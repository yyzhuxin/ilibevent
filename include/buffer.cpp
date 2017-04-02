#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include "log.hpp"
#include "buffer.hpp"



static const int EVBUFFER_MAX_READ = 4096;

static void evbuffer_align(struct evbuffer* buf)
{
	memmove(buf->orig_buffer, buf->buffer, buf->off);
	buf->buffer = buf->orig_buffer;
	buf->misalign = 0;
}


struct evbuffer* evbuffer_new(void)
{
	struct evbuffer* buffer = (struct evbuffer*)calloc(1, sizeof(struct evbuffer));
	return buffer;
}

void evbuffer_free(struct evbuffer* buffer)
{
	if (buffer->orig_buffer != NULL)
	{
		free(buffer->orig_buffer);
	}
	free(buffer);
}

int evbuffer_expand(struct evbuffer* buf, size_t datlen)
{
	size_t need = buf->misalign + buf->off + datlen;
	if (buf->totallen >= need)
	{
		return 0;
	}
	if (buf->misalign >= datlen) 
	{
		evbuffer_align(buf);
	} 
	else 
	{
		u_char* newbuf;
		size_t length = buf->totallen;
		if (length < 256)
		{
			length = 256;
		}
		while (length < need)
		{
			length <<= 1;
		}
		if (buf->orig_buffer != buf->buffer)
		{
			evbuffer_align(buf);
		}
		if ((newbuf = (u_char*)realloc(buf->buffer, length)) == NULL)
		{
			Error("realloc failed, errno = %d\n", errno);
			return -1;
		}
		buf->orig_buffer = buf->buffer = newbuf;
		buf->totallen = length;
	}
	return 0;
}

int evbuffer_add(struct evbuffer* buf, const void* data, size_t datlen)
{
	size_t need = buf->misalign + buf->off + datlen;
	size_t oldoff = buf->off;
	if (buf->totallen < need) 
	{
		if (evbuffer_expand(buf, datlen) == -1)
		{
			Error("evbuffer_expand failed\n");
			return -1;
		}
	}
	memcpy(buf->buffer + buf->off, data, datlen);
	buf->off += datlen;
	if (datlen && buf->cb != NULL)
	{
		(*buf->cb)(buf, oldoff, buf->off, buf->cbarg);
	}
	return 0;
}

int evbuffer_remove(struct evbuffer* buf, void* data, size_t datlen)
{
	size_t nread = datlen;
	if (nread >= buf->off)
	{
		nread = buf->off;
	}
	memcpy(data, buf->buffer, nread);
	evbuffer_drain(buf, nread);
	return nread;
}

void evbuffer_drain(struct evbuffer* buf, size_t len)
{
	size_t oldoff = buf->off;
	if (len >= buf->off) 
	{
		buf->off = 0;
		buf->buffer = buf->orig_buffer;
		buf->misalign = 0;
		goto done;
	}
	buf->buffer += len;
	buf->misalign += len;
	buf->off -= len;
done:
	if (buf->off != oldoff && buf->cb != NULL)
	{
		(*buf->cb)(buf, oldoff, buf->off, buf->cbarg);
	}
}

int evbuffer_read(struct evbuffer* buf, int fd, int howmuch)
{
	u_char* p;
	size_t oldoff = buf->off;
	int n = EVBUFFER_MAX_READ;

	if (ioctl(fd, FIONREAD, &n) == -1 || n <= 0) 
	{
		n = EVBUFFER_MAX_READ;
	} 
	else if (n > EVBUFFER_MAX_READ && n > howmuch) 
	{
		if ((size_t)n > buf->totallen << 2)
		{
			n = buf->totallen << 2;
		}
		if (n < EVBUFFER_MAX_READ)
		{
			n = EVBUFFER_MAX_READ;
		}
	}
	if (howmuch < 0 || howmuch > n)
	{
		howmuch = n;
	}
	if (evbuffer_expand(buf, howmuch) == -1)
	{
		Error("evbuffer_expand failed\n");
		return -1;
	}
	p = buf->buffer + buf->off;
	n = read(fd, p, howmuch);
	if (n == -1)
	{
		Error("read failed, errno = %d\n", errno);
		return -1;
	}
	if (n == 0)
	{
		return 0;
	}
	buf->off += n;
	if (buf->off != oldoff && buf->cb != NULL)
	{
		(*buf->cb)(buf, oldoff, buf->off, buf->cbarg);
	}
	return n;
}

int evbuffer_write(struct evbuffer* buffer, int fd)
{
	int n;
	n = write(fd, buffer->buffer, buffer->off);
	if (n == -1)
	{
		Error("write failed, errno = %d\n", errno);
		return -1;
	}
	if (n == 0)
	{
		return 0;
	}
	evbuffer_drain(buffer, n);
	return n;
}

void evbuffer_setcb(struct evbuffer* buffer, void (*cb)(struct evbuffer*, size_t, size_t, void*), void* cbarg)
{
	buffer->cb = cb;
	buffer->cbarg = cbarg;
}
