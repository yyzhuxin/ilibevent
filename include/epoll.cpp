#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include "log.hpp"
#include "signal.hpp"
#include "event.hpp"
#include "epoll.hpp"



static void* epoll_init(struct event_base*);
static int epoll_add(void*, struct event*);
static int epoll_del(void*, struct event*);
static int epoll_dispatch(struct event_base*, void*, struct timeval*);
static void epoll_dealloc(struct event_base*, void*);

const struct eventop epollops = 
{
	"epoll",
	epoll_init,
	epoll_add,
	epoll_del,
	epoll_dispatch,
	epoll_dealloc,
	1
};


static const int MAX_EPOLL_TIMEOUT_MSEC = (35 * 60 * 1000);
static const int INITIAL_NFILES = 32;
static const int INITIAL_NEVENTS = 32;
static const int MAX_NEVENTS = 4096;

static void* epoll_init(struct event_base* base)
{
	int epfd;
	struct epollop* epollop;

	if ((epfd = epoll_create(32000)) == -1) 
	{
		if (errno != ENOSYS)
		{
			Error("epoll_create failed, errno = %d", errno);
		}
		return (NULL);
	}

	if (fcntl(epfd, F_SETFD, 1) == -1)
	{
		Error("fcntl failed, errno = %d", errno);
	}

	if (!(epollop = (struct epollop*)calloc(1, sizeof(struct epollop))))
	{
		Error("calloc failed, errno = %d\n", errno);
		return (NULL);
	}
	epollop->epfd = epfd;
	epollop->events = (struct epoll_event*)malloc(INITIAL_NEVENTS * sizeof(struct epoll_event));
	if (epollop->events == NULL) 
	{
		free(epollop);
		return (NULL);
	}
	epollop->nevents = INITIAL_NEVENTS;

	epollop->fds = (struct evepoll*)calloc(INITIAL_NFILES, sizeof(struct evepoll));
	if (epollop->fds == NULL) 
	{
		free(epollop->events);
		free(epollop);
		return (NULL);
	}
	epollop->nfds = INITIAL_NFILES;

	evsignal_init(base);

	return (epollop);
}

static int epoll_recalc(struct event_base* base, void* arg, int max)
{
	struct epollop* epollop = (struct epollop*)arg;

	if (max >= epollop->nfds) 
	{
		struct evepoll *fds;
		int nfds = epollop->nfds;
		while (nfds <= max)
		{
			nfds <<= 1;
		}
		fds = (struct evepoll*)realloc(epollop->fds, nfds * sizeof(struct evepoll));
		if (fds == NULL) 
		{
			Error("realloc failed, errno = %d\n", errno);
			return (-1);
		}
		epollop->fds = fds;
		memset(fds + epollop->nfds, 0, (nfds - epollop->nfds) * sizeof(struct evepoll));
		epollop->nfds = nfds;
	}

	return (0);
}

static int epoll_dispatch(struct event_base* base, void* arg, struct timeval* tv)
{
	struct epollop* epollop = (struct epollop*)arg;
	struct epoll_event* events = epollop->events;
	struct evepoll* evep;
	int res;
	int timeout = -1;

	if (tv != NULL)
	{
		timeout = tv->tv_sec * 1000 + (tv->tv_usec + 999) / 1000;
	}
	if (timeout > MAX_EPOLL_TIMEOUT_MSEC) 
	{
		timeout = MAX_EPOLL_TIMEOUT_MSEC;
	}

	res = epoll_wait(epollop->epfd, events, epollop->nevents, timeout);

	if (res == -1) 
	{
		if (errno != EINTR) 
		{
			Error("epoll_wait failed, errno = %d\n", errno);
			return (-1);
		}

		evsignal_process(base);
		return (0);
	} 
	else if (base->sig->evsignal_caught) 
	{
		evsignal_process(base);
	}

	Debug("epoll_wait reports %d", res);

	for (int i = 0; i < res; ++i) 
	{
		int what = events[i].events;
		struct event* evread = NULL; 
		struct event* evwrite = NULL;
		int fd = events[i].data.fd;

		if (fd < 0 || fd >= epollop->nfds)
		{
			continue;
		}
		evep = &epollop->fds[fd];

		if (what & (EPOLLHUP | EPOLLERR)) 
		{
			evread = evep->evread;
			evwrite = evep->evwrite;
		} 
		else 
		{
			if (what & EPOLLIN) 
			{
				evread = evep->evread;
			}

			if (what & EPOLLOUT) 
			{
				evwrite = evep->evwrite;
			}
		}

		if (!(evread || evwrite))
		{
			continue;
		}
		if (evread != NULL)
		{
			event_active(evread, EV_READ, 1);
		}
		if (evwrite != NULL)
		{
			event_active(evwrite, EV_WRITE, 1);
		}
	}

	if (res == epollop->nevents && epollop->nevents < MAX_NEVENTS) 
	{
		int new_nevents = epollop->nevents * 2;
		struct epoll_event* new_events;

		new_events = (struct epoll_event*)realloc(epollop->events, new_nevents * sizeof(struct epoll_event));
		if (new_events) 
		{
			epollop->events = new_events;
			epollop->nevents = new_nevents;
		}
	}

	return (0);
}


static int epoll_add(void* arg, struct event* ev)
{
	struct epollop* epollop = (struct epollop*)arg;
	struct epoll_event epev = {0, {0}};
	struct evepoll* evep;
	int fd, op, events;

	if (ev->ev_events & EV_SIGNAL)
	{
		return (evsignal_add(ev));
	}
	fd = ev->ev_fd;
	if (fd >= epollop->nfds) 
	{
		if (epoll_recalc(ev->ev_base, epollop, fd) == -1)
		{
			return (-1);
		}
	}
	evep = &epollop->fds[fd];
	op = EPOLL_CTL_ADD;
	events = 0;
	if (evep->evread != NULL) 
	{
		events |= EPOLLIN;
		op = EPOLL_CTL_MOD;
	}
	if (evep->evwrite != NULL) 
	{
		events |= EPOLLOUT;
		op = EPOLL_CTL_MOD;
	}

	if (ev->ev_events & EV_READ)
	{
		events |= EPOLLIN;
	}
	if (ev->ev_events & EV_WRITE)
	{
		events |= EPOLLOUT;
	}
	epev.data.fd = fd;
	epev.events = events;
	if (epoll_ctl(epollop->epfd, op, ev->ev_fd, &epev) == -1)
	{
			return (-1);
	}
	if (ev->ev_events & EV_READ)
	{
		evep->evread = ev;
	}
	if (ev->ev_events & EV_WRITE)
	{
		evep->evwrite = ev;
	}
	return (0);
}

static int epoll_del(void* arg, struct event* ev)
{
	struct epollop* epollop = (struct epollop*)arg;
	struct epoll_event epev = {0, {0}};
	struct evepoll* evep;
	int fd, events, op;
	int needwritedelete = 1, needreaddelete = 1;

	if (ev->ev_events & EV_SIGNAL)
	{
		return (evsignal_del(ev));
	}
	fd = ev->ev_fd;
	if (fd >= epollop->nfds)
	{
		return (0);
	}
	evep = &epollop->fds[fd];

	op = EPOLL_CTL_DEL;
	events = 0;

	if (ev->ev_events & EV_READ)
	{
		events |= EPOLLIN;
	}
	if (ev->ev_events & EV_WRITE)
	{
		events |= EPOLLOUT;
	}
	if ((events & (EPOLLIN | EPOLLOUT)) != (EPOLLIN | EPOLLOUT)) 
	{
		if ((events & EPOLLIN) && evep->evwrite != NULL) 
		{
			needwritedelete = 0;
			events = EPOLLOUT;
			op = EPOLL_CTL_MOD;
		} 
		else if ((events & EPOLLOUT) && evep->evread != NULL) 
		{
			needreaddelete = 0;
			events = EPOLLIN;
			op = EPOLL_CTL_MOD;
		}
	}

	epev.events = events;
	epev.data.fd = fd;

	if (needreaddelete)
	{
		evep->evread = NULL;
	}
	if (needwritedelete)
	{
		evep->evwrite = NULL;
	}
	if (epoll_ctl(epollop->epfd, op, fd, &epev) == -1)
	{
		return (-1);
	}
	return (0);
}

static void epoll_dealloc(struct event_base* base, void* arg)
{
	struct epollop* epollop = (struct epollop*)arg;

	evsignal_dealloc(base);
	if (epollop->fds)
	{
		free(epollop->fds);
	}
	if (epollop->events)
	{
		free(epollop->events);
	}
	if (epollop->epfd >= 0)
	{
		close(epollop->epfd);
	}
	memset(epollop, 0, sizeof(struct epollop));
	free(epollop);
}

