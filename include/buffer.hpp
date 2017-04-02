#ifndef _BUFFER_HPP_
#define _BUFFER_HPP_

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char u_char;

struct evbuffer 
{
	u_char* buffer;
	u_char* orig_buffer;

	size_t misalign;
	size_t totallen;
	size_t off;

	void (*cb)(struct evbuffer*, size_t, size_t, void*);
	void* cbarg;
};



struct evbuffer* evbuffer_new(void);
void evbuffer_free(struct evbuffer* buffer);
int evbuffer_expand(struct evbuffer* buf, size_t datlen);
int evbuffer_add(struct evbuffer* buf, const void* data, size_t datlen);
int evbuffer_remove(struct evbuffer* buf, void* data, size_t datlen);
void evbuffer_drain(struct evbuffer* buf, size_t len);
int evbuffer_read(struct evbuffer* buf, int fd, int howmuch);
int evbuffer_write(struct evbuffer* buffer, int fd);
void evbuffer_setcb(struct evbuffer* buffer, void (*cb)(struct evbuffer*, size_t, size_t, void*), void* cbarg);


#ifdef __cplusplus
}
#endif

#endif
