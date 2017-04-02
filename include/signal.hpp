#ifndef _SIGNAL_HPP_
#define _SIGNAL_HPP_

#ifdef __cplusplus
extern "C" {
#endif

#include <signal.h>

struct event;
struct event_list;
struct evsignal_info 
{
	struct event* ev_signal;
	int ev_signal_added;
	int ev_signal_pair[2];
	volatile sig_atomic_t evsignal_caught;
	sig_atomic_t evsigcaught[NSIG];
	struct event_list* evsigevents[NSIG];
	struct sigaction **sh_old;
	int sh_old_max;
};


int evsignal_init(struct event_base* base);
int evsignal_add(struct event *ev);
int evsignal_del(struct event *ev);
void evsignal_process(struct event_base *base);
void evsignal_dealloc(struct event_base* base);

#ifdef __cplusplus
}
#endif


#endif
