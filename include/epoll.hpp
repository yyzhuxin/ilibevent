#ifndef _EPOLL_HPP_
#define _EPOLL_HPP_


#ifdef __cplusplus
extern "C" 
{
#endif


struct event;
struct evepoll 
{
	struct event* evread;
	struct event* evwrite;
};

struct epollop 
{
	struct evepoll* fds;
	int nfds;
	struct epoll_event* events;
	int nevents;
	int epfd;
};

struct eventop 
{
	const char* name;
	void* (*init)(struct event_base*);
	int (*add)(void*, struct event*);
	int (*del)(void*, struct event*);
	int (*dispatch)(struct event_base*, void*, struct timeval*);
	void (*dealloc)(struct event_base*, void*);
	int need_reinit;
};

#ifdef __cplusplus
}
#endif

#endif

