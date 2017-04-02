#include <stdlib.h>
#include <sys/queue.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include "log.hpp"
#include "event.hpp"
#include "signal.hpp"


struct event_base* evsignal_base = NULL;


static void evsignal_handler(int sig)
{
	int save_errno = errno;

	if (evsignal_base == NULL) 
	{
		Error("received signal %d, but evsignal_base didn't configured", sig);
		return;
	}

	++evsignal_base->sig->evsigcaught[sig];
	evsignal_base->sig->evsignal_caught = 1;

	signal(sig, evsignal_handler);

	send(evsignal_base->sig->ev_signal_pair[0], "a", 1, 0);
	errno = save_errno;
}

static int _evsignal_set_handler(struct event_base* base, int evsignal, void (*handler)(int))
{
	struct sigaction sa;
	struct evsignal_info* sig = base->sig;
	struct sigaction** p;

	if (evsignal >= sig->sh_old_max) 
	{
		int new_max = evsignal + 1;
		Debug("evsignal (%d) >= sh_old_max (%d), resizing", evsignal, sig->sh_old_max);
		p = (struct sigaction **)realloc(sig->sh_old, new_max * sizeof(*sig->sh_old));
		if (p == NULL) 
		{
			Error("realloc failed, errno = %d", errno);
			return (-1);
		}

		memset((char*)p + sig->sh_old_max * sizeof(*sig->sh_old), 0, (new_max - sig->sh_old_max) * sizeof(*sig->sh_old));

		sig->sh_old_max = new_max;
		sig->sh_old = (struct sigaction **)p;
	}

	sig->sh_old[evsignal] = (struct sigaction *)malloc(sizeof *sig->sh_old[evsignal]);
	if (sig->sh_old[evsignal] == NULL) 
	{
		Error("malloc failed, errno = %d", errno);
		return (-1);
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);

	if (sigaction(evsignal, &sa, sig->sh_old[evsignal]) == -1) 
	{
		Error("sigaction failed, errno = %d", errno);
		free(sig->sh_old[evsignal]);
		sig->sh_old[evsignal] = NULL;
		return (-1);
	}

	return (0);
}


static int _evsignal_restore_handler(struct event_base* base, int evsignal)
{
	int ret = 0;
	struct evsignal_info* sig = base->sig;
	struct sigaction* sh;

	sh = sig->sh_old[evsignal];
	sig->sh_old[evsignal] = NULL;
	if (sigaction(evsignal, sh, NULL) == -1) 
	{
		Error("sigaction failed, errno = %d", errno);
		ret = -1;
	}
	free(sh);

	return ret;
}

static void evsignal_cb(int fd, short what, void* arg)
{
	static char signals[1];
	ssize_t n;

	n = recv(fd, signals, sizeof(signals), 0);
	if (n == -1)
	{
		Error("read failed, errno = %d", errno);
	}
}

static int make_socket_nonblocking(int fd)
{
        long flags;
        if ((flags = fcntl(fd, F_GETFL, NULL)) < 0)
        {
                Error("fcntl(%d, F_GETFL)", fd);
                return -1;
        }

        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
                Error("fcntl(%d, F_SETFL)", fd);
                return -1;
        }

        return 0;
}



int evsignal_init(struct event_base* base)
{
	base->sig->ev_signal = (struct event*)calloc(1, sizeof(struct event));

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, base->sig->ev_signal_pair) == -1) 
	{
		Error("socketpair failed, errno = %d", errno);
		return -1;
	}

	if (fcntl(base->sig->ev_signal_pair[0], F_SETFD, 1) == -1)
	{
		Error("fcntl failed, errno = %d", errno);
	}

	if (fcntl(base->sig->ev_signal_pair[1], F_SETFD, 1) == -1)
	{
		Error("fcntl failed, errno = %d", errno);
	}


	base->sig->sh_old = NULL;
	base->sig->sh_old_max = 0;
	base->sig->evsignal_caught = 0;
	memset(&base->sig->evsigcaught, 0, sizeof(sig_atomic_t)*NSIG);


	for (int i = 0; i < NSIG; ++i)
	{
		base->sig->evsigevents[i] = (struct event_list*)calloc(1, sizeof(struct event_list));
	}

	for (int i = 0; i < NSIG; ++i)
	{
		(base->sig->evsigevents[i])->tqh_first = NULL;
		(base->sig->evsigevents[i])->tqh_last = &(base->sig->evsigevents[i])->tqh_first;
	}


    make_socket_nonblocking(base->sig->ev_signal_pair[0]);

	event_set(base->sig->ev_signal, base->sig->ev_signal_pair[1], EV_READ | EV_PERSIST, evsignal_cb, base->sig->ev_signal);
	base->sig->ev_signal->ev_base = base;
	base->sig->ev_signal->ev_flags |= EVLIST_INTERNAL;

	return 0;
}

int evsignal_add(struct event *ev)
{
	int evsignal;
	struct event_base *base = ev->ev_base;
	struct evsignal_info *sig = ev->ev_base->sig;

	if (ev->ev_events & (EV_READ|EV_WRITE))
	{
		Error("EV_SIGNAL incompatible use, ev->ev_events = %d", ev->ev_events);
	}
	evsignal = ev->ev_fd;
	assert(evsignal >= 0 && evsignal < NSIG);
	if (sig->evsigevents[evsignal]->tqh_first == NULL) 
	{
		Debug("%p: changing signal handler", ev);
		if (_evsignal_set_handler(base, evsignal, evsignal_handler) == -1)
		{
			return (-1);
		}
		evsignal_base = base;

		if (!sig->ev_signal_added) 
		{
			if (event_add(sig->ev_signal, NULL))
			{
				Error("event_add failed");
				return (-1);
			}
			sig->ev_signal_added = 1;
		}
	}

	(ev)->ev_signal_next.tqe_next = NULL;
	(ev)->ev_signal_next.tqe_prev = (sig->evsigevents[evsignal])->tqh_last;
	*(sig->evsigevents[evsignal])->tqh_last = (ev);
	(sig->evsigevents[evsignal])->tqh_last = &(ev)->ev_signal_next.tqe_next;

	return (0);
}

int evsignal_del(struct event *ev)
{
	struct event_base *base = ev->ev_base;
	struct evsignal_info *sig = base->sig;
	int evsignal = ev->ev_fd;

	assert(evsignal >= 0 && evsignal < NSIG);

	if (((ev)->ev_signal_next.tqe_next) != NULL)
	{
		(ev)->ev_signal_next.tqe_next->ev_signal_next.tqe_prev = (ev)->ev_signal_next.tqe_prev;	
	}
	else
	{
		(sig->evsigevents[evsignal])->tqh_last = (ev)->ev_signal_next.tqe_prev;
	}
	*(ev)->ev_signal_next.tqe_prev = (ev)->ev_signal_next.tqe_next;



	if (sig->evsigevents[evsignal]->tqh_first != NULL)
	{
		return (0);
	}
	Debug("%p: restoring signal handler", ev);

	return (_evsignal_restore_handler(ev->ev_base, ev->ev_fd));
}


void evsignal_process(struct event_base *base)
{
	struct evsignal_info *sig = base->sig;
	struct event *ev, *next_ev;
	sig_atomic_t ncalls;
	int i;

	base->sig->evsignal_caught = 0;
	for (i = 1; i < NSIG; ++i) 
	{
		ncalls = sig->evsigcaught[i];
		if (ncalls == 0)
		{
			continue;
		}
		sig->evsigcaught[i] -= ncalls;

		for (ev = sig->evsigevents[i]->tqh_first; ev != NULL; ev = next_ev) 
		{
			next_ev = ev->ev_signal_next.tqe_next;
			if (!(ev->ev_events & EV_PERSIST))
			{
				event_del(ev);
			}
			event_active(ev, EV_SIGNAL, ncalls);
		}

	}
}

void evsignal_dealloc(struct event_base* base)
{
	int i = 0;
	if (base->sig->ev_signal_added) 
	{
		event_del(base->sig->ev_signal);
		base->sig->ev_signal_added = 0;
	}
	for (i = 0; i < NSIG; ++i) 
	{
		if (i < base->sig->sh_old_max && base->sig->sh_old[i] != NULL)
		{
			_evsignal_restore_handler(base, i);
		}
	}

	if (base->sig->ev_signal_pair[0] != -1) 
	{
		close(base->sig->ev_signal_pair[0]);
		base->sig->ev_signal_pair[0] = -1;
	}
	if (base->sig->ev_signal_pair[1] != -1) 
	{
		close(base->sig->ev_signal_pair[1]);
		base->sig->ev_signal_pair[1] = -1;
	}
	base->sig->sh_old_max = 0;

	if (base->sig->sh_old) 
	{
		free(base->sig->sh_old);
		base->sig->sh_old = NULL;
	}


	free(base->sig->ev_signal);
}
