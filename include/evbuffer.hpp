#ifndef _EVBUFFER_HPP_
#define _EVBUFFER_HPP_


#ifdef __cplusplus
extern "C" {
#endif

#include "event.hpp"

#define EVBUFFER_READ		0x01
#define EVBUFFER_WRITE		0x02
#define EVBUFFER_EOF		0x10
#define EVBUFFER_ERROR		0x20
#define EVBUFFER_TIMEOUT	0x40

struct bufferevent;
typedef void (*evbuffercb)(struct bufferevent *, void *);
typedef void (*everrorcb)(struct bufferevent *, short what, void *);

struct event_watermark 
{
	size_t low;
	size_t high;
};

struct event_base;
struct evbuffer;
struct bufferevent 
{
	struct event_base* ev_base;

	struct event ev_read;
	struct event ev_write;

	struct evbuffer* input;
	struct evbuffer* output;

	struct event_watermark wm_read;
	struct event_watermark wm_write;

	evbuffercb readcb;
	evbuffercb writecb;
	everrorcb errorcb;
	void* cbarg;

	int timeout_read;
	int timeout_write;

	short enabled;
};




struct bufferevent* bufferevent_new(int fd, evbuffercb readcb, evbuffercb writecb, everrorcb errorcb, void* cbarg);
int bufferevent_base_set(struct event_base* base, struct bufferevent* bufev);
int bufferevent_priority_set(struct bufferevent* bufev, int priority);
void bufferevent_free(struct bufferevent* bufev);
void bufferevent_setcb(struct bufferevent* bufev, evbuffercb readcb, evbuffercb writecb, everrorcb errorcb, void* cbarg);
void bufferevent_setfd(struct bufferevent* bufev, int fd);
int bufferevent_write(struct bufferevent* bufev, const void* data, size_t size);
int bufferevent_write_buffer(struct bufferevent* bufev, struct evbuffer* buf);
size_t bufferevent_read(struct bufferevent* bufev, void* data, size_t size);
int bufferevent_enable(struct bufferevent* bufev, short event);
int bufferevent_disable(struct bufferevent* bufev, short event);
void bufferevent_settimeout(struct bufferevent* bufev, int timeout_read, int timeout_write);
void bufferevent_read_pressure_cb(struct evbuffer* buf, size_t old, size_t now, void *arg);
void bufferevent_setwatermark(struct bufferevent* bufev, short events, size_t lowmark, size_t highmark);

#ifdef __cplusplus
}
#endif

#endif



























