#include <sys/queue.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include "log.hpp"
#include "minheap.hpp"
#include "signal.hpp"
#include "epoll.hpp"
#include "event.hpp"

struct event_base* current_base = NULL;
int (*event_sigcb)(void);
volatile sig_atomic_t event_gotsig;

static const struct eventop *eventops[] = 
{
	&epollops,
	NULL
};


static int use_monotonic;

static void	event_queue_insert(struct event_base*, struct event*, int);
static void	event_queue_remove(struct event_base*, struct event*, int);
static int	event_haveevents(struct event_base*);

static void	event_process_active(struct event_base*);

static int	timeout_next(struct event_base*, struct timeval**);
static void	timeout_process(struct event_base*);
static void	timeout_correct(struct event_base*, struct timeval*);

static void detect_monotonic(void)
{
	struct timespec	ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
	{
		use_monotonic = 1;
	}
}

static int gettime(struct event_base* base, struct timeval* tp)
{
	if (base->tv_cache.tv_sec) 
	{
		*tp = base->tv_cache;
		return 0;
	}

	if (use_monotonic) 
	{
		struct timespec	ts;

		if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
		{
			Error("clock_gettime failed, errno = %d", errno);
			return -1;
		}
		tp->tv_sec = ts.tv_sec;
		tp->tv_usec = ts.tv_nsec / 1000;
		return 0;
	}

	return gettimeofday(tp, NULL);
}

struct event_base* event_init(void)
{
	struct event_base* base = event_base_new();

	if (base != NULL)
	{
		current_base = base;
	}
	return base;
}

struct event_base* event_base_new(void)
{
	struct event_base* base;

	if ((base = (struct event_base*)calloc(1, sizeof(struct event_base))) == NULL)
	{
		Error("calloc failed, errno = %d", errno);
	}
	event_sigcb = NULL;
	event_gotsig = 0;

	detect_monotonic();
	gettime(base, &base->event_tv);
	
	base->timeheap = (struct min_heap*)calloc(1, sizeof(struct min_heap));
	min_heap_ctor(base->timeheap);
 
	base->eventqueue = (struct event_list*)calloc(1, sizeof(struct event_list));
	if (base->eventqueue == NULL)
	{
		Error("calloc failed, errno = %d", errno);
	}
	(base->eventqueue)->tqh_first = NULL;
	(base->eventqueue)->tqh_last = &(base->eventqueue)->tqh_first;

	base->sig = (struct evsignal_info*)calloc(1, sizeof(struct evsignal_info));
	base->sig->ev_signal_pair[0] = -1;
	base->sig->ev_signal_pair[1] = -1;
	
	base->evbase = NULL;
	for (int i = 0; eventops[i] && !base->evbase; ++i) 
	{
		base->evsel = eventops[i];
		base->evbase = base->evsel->init(base);
	}

	if (base->evbase == NULL)
	{
		Error("no event mechanism available");
	}


	event_base_priority_init(base, 1);

	return (base);
}

void event_base_free(struct event_base* base)
{
	int i, n_deleted=0;
	struct event* ev;

	if (base == NULL && current_base)
	{
		base = current_base;
	}
	if (base == current_base)
	{
		current_base = NULL;
	}
	assert(base);
	for (ev = base->eventqueue->tqh_first; ev; ) 
	{
		struct event *next = ev->ev_next.tqe_next;
		if (!(ev->ev_flags & EVLIST_INTERNAL)) 
		{
			event_del(ev);
			++n_deleted;
		}
		ev = next;
	}
	while ((ev = min_heap_top(base->timeheap)) != NULL) 
	{
		event_del(ev);
		++n_deleted;
	}

	for (i = 0; i < base->nactivequeues; ++i) 
	{
		for (ev = base->activequeues[i]->tqh_first; ev; ) 
		{
			struct event *next = ev->ev_active_next.tqe_next;
			if (!(ev->ev_flags & EVLIST_INTERNAL)) 
			{
				event_del(ev);
				++n_deleted;
			}
			ev = next;
		}
	}

	if (n_deleted)
	{
		Debug("%d events were still set in base", n_deleted);
	}
	if (base->evsel->dealloc != NULL)
	{
		base->evsel->dealloc(base, base->evbase);
	}
	for (i = 0; i < base->nactivequeues; ++i)
	{
		assert(base->activequeues[i]->tqh_first == NULL);
	}
	assert(min_heap_empty(base->timeheap));
	min_heap_dtor(base->timeheap);

	for (i = 0; i < base->nactivequeues; ++i)
	{
		free(base->activequeues[i]);
	}
	free(base->activequeues);

	free(base->timeheap);

	free(base->sig);

	assert(base->eventqueue->tqh_first == NULL);

	free(base->eventqueue);
	free(base);
}

int event_reinit(struct event_base *base)
{
	const struct eventop *evsel = base->evsel;
	void *evbase = base->evbase;
	int res = 0;
	struct event *ev;

	if (!evsel->need_reinit)
	{
		return (0);
	}
	if (base->sig->ev_signal_added) 
	{
		event_queue_remove(base, base->sig->ev_signal, EVLIST_INSERTED);
		if (base->sig->ev_signal->ev_flags & EVLIST_ACTIVE)
		{
			event_queue_remove(base, base->sig->ev_signal, EVLIST_ACTIVE);
		}
		base->sig->ev_signal_added = 0;
	}
	
	if (base->evsel->dealloc != NULL)
	{
		base->evsel->dealloc(base, base->evbase);
	}
	evbase = base->evbase = evsel->init(base);
	if (base->evbase == NULL)
	{
		Error("could not reinitialize event mechanism");
	}
	for(ev = base->eventqueue->tqh_first; ev != NULL; ev = ev->ev_next.tqe_next)
	{
		if (evsel->add(evbase, ev) == -1)
		{
			res = -1;
		}
	}

	return (res);
}

int event_priority_init(int npriorities)
{
  return event_base_priority_init(current_base, npriorities);
}

int event_base_priority_init(struct event_base *base, int npriorities)
{
	int i;

	if (base->event_count_active)
	{
		return (-1);
	}
	if (npriorities == base->nactivequeues)
	{
		return (0);
	}
	if (base->nactivequeues) 
	{
		for (i = 0; i < base->nactivequeues; ++i) 
		{
			free(base->activequeues[i]);
		}
		free(base->activequeues);
	}

	base->nactivequeues = npriorities;
	base->activequeues = (struct event_list **)calloc(base->nactivequeues, sizeof(struct event_list *));
	if (base->activequeues == NULL)
	{
		Error("calloc failed, errno = %d", errno);
	}



	for (i = 0; i < base->nactivequeues; ++i) 
	{
		base->activequeues[i] = (struct event_list*)malloc(sizeof(struct event_list));
		if (base->activequeues[i] == NULL)
		{
			Error("malloc failed, errno = %d", errno);
		}
		base->activequeues[i]->tqh_first = NULL;
		base->activequeues[i]->tqh_last = &base->activequeues[i]->tqh_first;
	}

	return (0);
}

int event_haveevents(struct event_base* base)
{
	return (base->event_count > 0);
}

static void event_process_active(struct event_base* base)
{
	struct event *ev;
	struct event_list *activeq = NULL;
	int i;
	short ncalls;

	for (i = 0; i < base->nactivequeues; ++i) 
	{
		if (base->activequeues[i]->tqh_first != NULL) 
		{
			activeq = base->activequeues[i];
			break;
		}
	}

	assert(activeq != NULL);

	for (ev = activeq->tqh_first; ev; ev = activeq->tqh_first) 
	{
		if (ev->ev_events & EV_PERSIST)
		{
			event_queue_remove(base, ev, EVLIST_ACTIVE);
		}
		else
		{
			event_del(ev);
		}
		ncalls = ev->ev_ncalls;
		ev->ev_pncalls = &ncalls;
		while (ncalls) 
		{
			ncalls--;
			ev->ev_ncalls = ncalls;
			(*ev->ev_callback)((int)ev->ev_fd, ev->ev_res, ev->ev_arg);
			if (event_gotsig || base->event_break)
			{
				return;
			}
		}
	}
}

int event_dispatch(void)
{
	return (event_loop(0));
}

int event_base_dispatch(struct event_base *event_base)
{
  return (event_base_loop(event_base, 0));
}

static void event_loopexit_cb(int fd, short what, void *arg)
{
	struct event_base *base = (struct event_base*)arg;
	base->event_gotterm = 1;
}

int event_loopexit(const struct timeval *tv)
{
	return (event_once(-1, EV_TIMEOUT, event_loopexit_cb, current_base, tv));
}

int event_base_loopexit(struct event_base *event_base, const struct timeval *tv)
{
	return (event_base_once(event_base, -1, EV_TIMEOUT, event_loopexit_cb, event_base, tv));
}

int event_loopbreak(void)
{
	return (event_base_loopbreak(current_base));
}

int event_base_loopbreak(struct event_base *event_base)
{
	if (event_base == NULL)
	{
		return (-1);
	}
	event_base->event_break = 1;
	return (0);
}


int event_loop(int flags)
{
	return event_base_loop(current_base, flags);
}

int event_base_loop(struct event_base *base, int flags)
{
	const struct eventop *evsel = base->evsel;
	void *evbase = base->evbase;
	struct timeval tv;
	struct timeval *tv_p;
	int res, done;

	base->tv_cache.tv_sec = 0;

	if (base->sig->ev_signal_added)
	{
		evsignal_base = base;
	}
	done = 0;
	while (!done) 
	{
		if (base->event_gotterm) 
		{
			base->event_gotterm = 0;
			break;
		}

		if (base->event_break) 
		{
			base->event_break = 0;
			break;
		}
		while (event_gotsig) 
		{
			event_gotsig = 0;
			if (event_sigcb) 
			{
				res = (*event_sigcb)();
				if (res == -1) 
				{
					errno = EINTR;
					return (-1);
				}
			}
		}

		timeout_correct(base, &tv);

		tv_p = &tv;
		if (!base->event_count_active && !(flags & EVLOOP_NONBLOCK)) 
		{
			timeout_next(base, &tv_p);
		} 
		else 
		{
			timerclear(&tv);
		}
		
		if (!event_haveevents(base)) 
		{
			Debug("no events registered.");
			return (1);
		}

		gettime(base, &base->event_tv);

		base->tv_cache.tv_sec = 0;

		res = evsel->dispatch(base, evbase, tv_p);

		if (res == -1)
		{
			return (-1);
		}
		gettime(base, &base->tv_cache);

		timeout_process(base);

		if (base->event_count_active) 
		{
			event_process_active(base);
			if (!base->event_count_active && (flags & EVLOOP_ONCE))
			{
				done = 1;
			}
		} 
		else if (flags & EVLOOP_NONBLOCK)
		{
			done = 1;
		}
	}

	base->tv_cache.tv_sec = 0;

	Debug("asked to terminate loop.");
	return (0);
}


struct event_once 
{
	struct event ev;

	void (*cb)(int, short, void *);
	void *arg;
};


static void event_once_cb(int fd, short events, void *arg)
{
	struct event_once *eonce = (struct event_once*)arg;

	(*eonce->cb)(fd, events, eonce->arg);
	free(eonce);
}

int event_once(int fd, short events, void (*callback)(int, short, void *), void *arg, const struct timeval *tv)
{
	return event_base_once(current_base, fd, events, callback, arg, tv);
}

int event_base_once(struct event_base *base, int fd, short events, void (*callback)(int, short, void *), void *arg, const struct timeval *tv)
{
	struct event_once *eonce;
	struct timeval etv;
	int res;

	if (events & EV_SIGNAL)
	{
		return (-1);
	}
	if ((eonce = (struct event_once*)calloc(1, sizeof(struct event_once))) == NULL)
	{
		return (-1);
	}
	eonce->cb = callback;
	eonce->arg = arg;

	if (events == EV_TIMEOUT) 
	{
		if (tv == NULL) 
		{
			timerclear(&etv);
			tv = &etv;
		}

		evtimer_set(&eonce->ev, event_once_cb, eonce);
	} 
	else if (events & (EV_READ|EV_WRITE)) 
	{
		events &= EV_READ|EV_WRITE;

		event_set(&eonce->ev, fd, events, event_once_cb, eonce);
	} 
	else 
	{
		free(eonce);
		return (-1);
	}

	res = event_base_set(base, &eonce->ev);
	if (res == 0)
	{
		res = event_add(&eonce->ev, tv);
	}
	if (res != 0) 
	{
		free(eonce);
		return (res);
	}

	return (0);
}

void event_set(struct event *ev, int fd, short events, void (*callback)(int, short, void *), void *arg)
{
	ev->ev_base = current_base;

	ev->ev_callback = callback;
	ev->ev_arg = arg;
	ev->ev_fd = fd;
	ev->ev_events = events;
	ev->ev_res = 0;
	ev->ev_flags = EVLIST_INIT;
	ev->ev_ncalls = 0;
	ev->ev_pncalls = NULL;

	min_heap_elem_init(ev);

	if(current_base)
	{
		ev->ev_pri = current_base->nactivequeues/2;
	}
}

int event_base_set(struct event_base* base, struct event* ev)
{
	if (ev->ev_flags != EVLIST_INIT)
	{
		return (-1);
	}
	ev->ev_base = base;
	ev->ev_pri = base->nactivequeues/2;

	return (0);
}

int event_priority_set(struct event* ev, int pri)
{
	if (ev->ev_flags & EVLIST_ACTIVE)
	{
		return (-1);
	}
	if (pri < 0 || pri >= ev->ev_base->nactivequeues)
	{
		return (-1);
	}
	ev->ev_pri = pri;

	return (0);
}

int event_pending(struct event* ev, short event, struct timeval* tv)
{
	struct timeval	now, res;
	int flags = 0;

	if (ev->ev_flags & EVLIST_INSERTED)
	{
		flags |= (ev->ev_events & (EV_READ|EV_WRITE|EV_SIGNAL));
	}
	if (ev->ev_flags & EVLIST_ACTIVE)
	{
		flags |= ev->ev_res;
	}
	if (ev->ev_flags & EVLIST_TIMEOUT)
	{
		flags |= EV_TIMEOUT;
	}
	event &= (EV_TIMEOUT|EV_READ|EV_WRITE|EV_SIGNAL);

	if (tv != NULL && (flags & event & EV_TIMEOUT)) 
	{
		gettime(ev->ev_base, &now);
		timersub(&ev->ev_timeout, &now, &res);
		gettimeofday(&now, NULL);
		timeradd(&now, &res, tv);
	}

	return (flags & event);
}

int event_add(struct event* ev, const struct timeval* tv)
{
	struct event_base* base = ev->ev_base;
	const struct eventop* evsel = base->evsel;
	void* evbase = base->evbase;
	int res = 0;

	Debug(
		 "event_add: event: %p, %s%s%scall %p",
		 ev,
		 ev->ev_events & EV_READ ? "EV_READ " : " ",
		 ev->ev_events & EV_WRITE ? "EV_WRITE " : " ",
		 tv ? "EV_TIMEOUT " : " ",
		 ev->ev_callback);

	assert(!(ev->ev_flags & ~EVLIST_ALL));

	if (tv != NULL && !(ev->ev_flags & EVLIST_TIMEOUT)) 
	{
		if (min_heap_reserve(base->timeheap, 1 + min_heap_size(base->timeheap)) == -1)
		{
			Error("min_heap_reserve failed");
			return (-1);
		}
	}

	if ((ev->ev_events & (EV_READ|EV_WRITE|EV_SIGNAL)) && !(ev->ev_flags & (EVLIST_INSERTED|EVLIST_ACTIVE))) 
	{
		res = evsel->add(evbase, ev);
		if (res != -1)
		{
			event_queue_insert(base, ev, EVLIST_INSERTED);
		}
	}

	if (res != -1 && tv != NULL) 
	{
		struct timeval now;

		if (ev->ev_flags & EVLIST_TIMEOUT)
		{
			event_queue_remove(base, ev, EVLIST_TIMEOUT);
		}
		if ((ev->ev_flags & EVLIST_ACTIVE) && (ev->ev_res & EV_TIMEOUT)) 
		{
			if (ev->ev_ncalls && ev->ev_pncalls) 
			{
				*ev->ev_pncalls = 0;
			}
			
			event_queue_remove(base, ev, EVLIST_ACTIVE);
		}

		gettime(base, &now);
		timeradd(&now, tv, &ev->ev_timeout);

		Debug("event_add: timeout in %ld seconds, call %p", tv->tv_sec, ev->ev_callback);

		event_queue_insert(base, ev, EVLIST_TIMEOUT);
	}

	return (res);
}

int event_del(struct event* ev)
{
	struct event_base* base;
	const struct eventop* evsel;
	void *evbase;

	Debug("event_del: %p, callback %p", ev, ev->ev_callback);

	if (ev->ev_base == NULL)
	{
		return (-1);
	}
	base = ev->ev_base;
	evsel = base->evsel;
	evbase = base->evbase;

	assert(!(ev->ev_flags & ~EVLIST_ALL));

	if (ev->ev_ncalls && ev->ev_pncalls) 
	{
		*ev->ev_pncalls = 0;
	}

	if (ev->ev_flags & EVLIST_TIMEOUT)
	{
		event_queue_remove(base, ev, EVLIST_TIMEOUT);
	}
	if (ev->ev_flags & EVLIST_ACTIVE)
	{
		event_queue_remove(base, ev, EVLIST_ACTIVE);
	}
	if (ev->ev_flags & EVLIST_INSERTED) 
	{
		event_queue_remove(base, ev, EVLIST_INSERTED);
		return (evsel->del(evbase, ev));
	}

	return (0);
}

void event_active(struct event* ev, int res, short ncalls)
{
	if (ev->ev_flags & EVLIST_ACTIVE) 
	{
		ev->ev_res |= res;
		return;
	}

	ev->ev_res = res;
	ev->ev_ncalls = ncalls;
	ev->ev_pncalls = NULL;
	event_queue_insert(ev->ev_base, ev, EVLIST_ACTIVE);
}

static int timeout_next(struct event_base* base, struct timeval** tv_p)
{
	struct timeval now;
	struct event *ev;
	struct timeval *tv = *tv_p;

	if ((ev = min_heap_top(base->timeheap)) == NULL) 
	{
		*tv_p = NULL;
		return (0);
	}

	if (gettime(base, &now) == -1)
	{
		return (-1);
	}
	if (timercmp(&ev->ev_timeout, &now, <=)) 
	{
		timerclear(tv);
		return (0);
	}

	timersub(&ev->ev_timeout, &now, tv);

	assert(tv->tv_sec >= 0);
	assert(tv->tv_usec >= 0);

	Debug("timeout_next: in %ld seconds", tv->tv_sec);
	return (0);
}

static void timeout_correct(struct event_base* base, struct timeval* tv)
{
	struct event** pev;
	unsigned int size;
	struct timeval off;

	if (use_monotonic)
	{
		return;
	}
	gettime(base, tv);
	if (timercmp(tv, &base->event_tv, >=)) 
	{
		base->event_tv = *tv;
		return;
	}

	Debug("%s: time is running backwards, corrected", __func__);
	timersub(&base->event_tv, tv, &off);

	pev = base->timeheap->p;
	size = base->timeheap->n;
	for (; size-- > 0; ++pev) 
	{
		struct timeval* ev_tv = &(**pev).ev_timeout;
		timersub(ev_tv, &off, ev_tv);
	}
	base->event_tv = *tv;
}

void timeout_process(struct event_base* base)
{
	struct timeval now;
	struct event* ev;

	if (min_heap_empty(base->timeheap))
	{
		return;
	}
	gettime(base, &now);

	while ((ev = min_heap_top(base->timeheap))) 
	{
		if (timercmp(&ev->ev_timeout, &now, >))
		{
			break;
		}
		event_del(ev);

		Debug("timeout_process: call %p", ev->ev_callback);
		event_active(ev, EV_TIMEOUT, 1);
	}
}

void event_queue_remove(struct event_base* base, struct event* ev, int queue)
{
	if (!(ev->ev_flags & queue))
	{
		Error("%p(fd %d) not on queue %x", ev, ev->ev_fd, queue);
	}
	if (~ev->ev_flags & EVLIST_INTERNAL)
	{
		--base->event_count;
	}
	ev->ev_flags &= ~queue;
	switch (queue) 
	{
	case EVLIST_INSERTED:
		if (ev->ev_next.tqe_next != NULL)
		{
			ev->ev_next.tqe_next->ev_next.tqe_prev = ev->ev_next.tqe_prev;
		}
		else
		{
			base->eventqueue->tqh_last = ev->ev_next.tqe_prev;
		}
		*ev->ev_next.tqe_prev = ev->ev_next.tqe_next;

		break;
	case EVLIST_ACTIVE:
		--base->event_count_active;
		if (ev->ev_active_next.tqe_next != NULL)
		{
			ev->ev_active_next.tqe_next->ev_active_next.tqe_prev =	ev->ev_active_next.tqe_prev;
		}
		else
		{
			base->activequeues[ev->ev_pri]->tqh_last = ev->ev_active_next.tqe_prev;
		}
		*ev->ev_active_next.tqe_prev = ev->ev_active_next.tqe_next;
		
		break;
	case EVLIST_TIMEOUT:
		min_heap_erase(base->timeheap, ev);
		break;
	default:
		Error("unknown queue %x", queue);
	}
}

void event_queue_insert(struct event_base* base, struct event* ev, int queue)
{
	if (ev->ev_flags & queue) 
	{
		if (queue & EVLIST_ACTIVE)
		{
			return;
		}
		Error("%p(fd %d) already on queue %x", ev, ev->ev_fd, queue);
	}

	if (~ev->ev_flags & EVLIST_INTERNAL)
	{
		++base->event_count;
	}
	ev->ev_flags |= queue;
	switch (queue) 
	{
	case EVLIST_INSERTED:
		ev->ev_next.tqe_next = NULL;
		ev->ev_next.tqe_prev = base->eventqueue->tqh_last;
		*base->eventqueue->tqh_last = ev;
		base->eventqueue->tqh_last = &ev->ev_next.tqe_next;

		break;
	case EVLIST_ACTIVE:
		++base->event_count_active;

		ev->ev_active_next.tqe_next = NULL;
		ev->ev_active_next.tqe_prev = base->activequeues[ev->ev_pri]->tqh_last;
		*base->activequeues[ev->ev_pri]->tqh_last = ev;
		base->activequeues[ev->ev_pri]->tqh_last = &ev->ev_active_next.tqe_next;


		break;
	case EVLIST_TIMEOUT: 
		{
		min_heap_push(base->timeheap, ev);
		break;
		}
	default:
		Error("unknown queue %x", queue);
	}
}

