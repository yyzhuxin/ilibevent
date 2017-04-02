#ifndef _EVENT_HPP_
#define _EVENT_HPP_


#ifdef __cplusplus
extern "C" {
#endif






#define EVLIST_TIMEOUT	0x01
#define EVLIST_INSERTED	0x02
#define EVLIST_SIGNAL	0x04
#define EVLIST_ACTIVE	0x08
#define EVLIST_INTERNAL	0x10
#define EVLIST_INIT	0x80
#define EVLIST_ALL	(0xf000 | 0x9f)



#define EV_TIMEOUT	0x01
#define EV_READ		0x02
#define EV_WRITE	0x04
#define EV_SIGNAL	0x08
#define EV_PERSIST	0x10

struct event_base;
struct event 
{
	struct 
	{ 
		struct event* tqe_next;  
		struct event** tqe_prev; 
	} ev_next;
	struct 
	{ 
		struct event* tqe_next;  
		struct event** tqe_prev; 
	} ev_active_next;
	struct 
	{ 
		struct event* tqe_next;  
		struct event** tqe_prev; 
	} ev_signal_next;
	unsigned int min_heap_idx;

	struct event_base* ev_base;

	int ev_fd;
	short ev_events;
	short ev_ncalls;
	short* ev_pncalls;

	struct timeval ev_timeout;

	int ev_pri;

	void (*ev_callback)(int, short, void*);
	void* ev_arg;

	int ev_res;
	int ev_flags;
};

struct event_list  
{  
	struct event* tqh_first;  
	struct event** tqh_last;  
};  

struct eventop;
struct min_heap;
struct evsignal_info;
struct event_base 
{
	const struct eventop* evsel;
	void *evbase;
	int event_count;
	int event_count_active;

	int event_gotterm;
	int event_break;

	struct event_list** activequeues;
	int nactivequeues;

	struct evsignal_info* sig;

	struct event_list* eventqueue;
	struct timeval event_tv;

	struct min_heap* timeheap;

	struct timeval tv_cache;
};

extern const struct eventop epollops;
extern struct event_base* evsignal_base;



struct event_base* event_base_new(void);
struct event_base* event_init(void);
int event_reinit(struct event_base* base);
int event_dispatch(void);
int event_base_dispatch(struct event_base*);
void event_base_free(struct event_base*);
int event_base_set(struct event_base*, struct event*);

#define EVLOOP_ONCE	0x01
#define EVLOOP_NONBLOCK	0x02

int event_loop(int);
int event_base_loop(struct event_base*, int);
int event_loopexit(const struct timeval*);
int event_base_loopexit(struct event_base*, const struct timeval*);
int event_loopbreak(void);
int event_base_loopbreak(struct event_base*);

#define evtimer_add(ev, tv)	event_add(ev, tv)
#define evtimer_set(ev, cb, arg) event_set(ev, -1, 0, cb, arg)
#define evtimer_del(ev)	event_del(ev)
#define evtimer_pending(ev, tv)	event_pending(ev, EV_TIMEOUT, tv)
#define evtimer_initialized(ev)	((ev)->ev_flags & EVLIST_INIT)


#define timeout_add(ev, tv)	event_add(ev, tv)
#define timeout_set(ev, cb, arg) event_set(ev, -1, 0, cb, arg)
#define timeout_del(ev)	event_del(ev)
#define timeout_pending(ev, tv)	event_pending(ev, EV_TIMEOUT, tv)
#define timeout_initialized(ev)	((ev)->ev_flags & EVLIST_INIT)


#define signal_add(ev, tv)	event_add(ev, tv)
#define signal_set(ev, x, cb, arg)	event_set(ev, x, EV_SIGNAL | EV_PERSIST, cb, arg)
#define signal_del(ev) event_del(ev)
#define signal_pending(ev, tv) event_pending(ev, EV_SIGNAL, tv)
#define signal_initialized(ev) ((ev)->ev_flags & EVLIST_INIT)

void event_set(struct event*, int, short, void (*)(int, short, void*), void*);
int event_once(int, short, void (*)(int, short, void*), void*, const struct timeval*);
int event_base_once(struct event_base*base, int fd, short events, void (*callback)(int, short, void*), void* arg, const struct timeval* timeout);
int event_add(struct event* ev, const struct timeval* timeout);
int event_del(struct event*);
void event_active(struct event*, int, short);
int event_pending(struct event*ev, short event, struct timeval* tv);
#define event_initialized(ev) ((ev)->ev_flags & EVLIST_INIT)
int	event_priority_init(int);
int	event_base_priority_init(struct event_base*, int);
int	event_priority_set(struct event*, int);


#ifdef __cplusplus
}
#endif

#endif
