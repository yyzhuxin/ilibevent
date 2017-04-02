#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include "log.hpp"
#include "event.hpp"
#include "minheap.hpp"




void min_heap_ctor(min_heap_t* s)
{
	s->p = 0; 
	s->n = 0; 
	s->a = 0; 
}

void min_heap_dtor(min_heap_t* s)
{ 
	if(s->p) 
	{
		free(s->p); 
	}
}

void min_heap_elem_init(struct event* e)
{ 
	e->min_heap_idx = -1; 
}

int min_heap_elem_greater(struct event* a, struct event* b)
{
	return timercmp(&a->ev_timeout, &b->ev_timeout, >);
}

int min_heap_empty(min_heap_t* s)
{ 
	return 0u == s->n; 
}

unsigned min_heap_size(min_heap_t* s)
{ 
	return s->n; 
}

struct event*  min_heap_top(min_heap_t* s)
{ 
	return s->n ? *s->p : 0; 
}

int min_heap_reserve(min_heap_t* s, unsigned n)
{
	if(s->a < n)
	{
		struct event** p;
		unsigned a = s->a ? s->a * 2 : 8;
		if(a < n)
		{
			a = n;
		}
		if(!(p = (struct event**)realloc(s->p, a * sizeof *p)))
		{
			Error("realloc failed, errno = %d\n", errno);
			return -1;
		}
		s->p = p;
		s->a = a;
	}
	return 0;
}

void min_heap_shift_up_(min_heap_t* s, unsigned hole_index, struct event* e)
{
	unsigned parent = (hole_index - 1) / 2;
	while(hole_index && min_heap_elem_greater(s->p[parent], e))
	{
		(s->p[hole_index] = s->p[parent])->min_heap_idx = hole_index;
		hole_index = parent;
		parent = (hole_index - 1) / 2;
	}
	(s->p[hole_index] = e)->min_heap_idx = hole_index;
}

void min_heap_shift_down_(min_heap_t* s, unsigned hole_index, struct event* e)
{
	unsigned min_child = 2 * (hole_index + 1);
	while(min_child <= s->n)
	{
		min_child -= min_child == s->n || min_heap_elem_greater(s->p[min_child], s->p[min_child - 1]);
		if(!(min_heap_elem_greater(e, s->p[min_child])))
		{
			break;
		}
		(s->p[hole_index] = s->p[min_child])->min_heap_idx = hole_index;
		hole_index = min_child;
		min_child = 2 * (hole_index + 1);
	}
	min_heap_shift_up_(s, hole_index,  e);
}

int min_heap_push(min_heap_t* s, struct event* e)
{
	if(min_heap_reserve(s, s->n + 1))
	{
		Error("min_heap_reserve failed\n");
		return -1;
	}
	min_heap_shift_up_(s, s->n++, e);
	return 0;
}

struct event* min_heap_pop(min_heap_t* s)
{
	if(s->n)
	{
		struct event* e = *s->p;
		min_heap_shift_down_(s, 0u, s->p[--s->n]);
		e->min_heap_idx = -1;
		return e;
	}
	return 0;
}

int min_heap_erase(min_heap_t* s, struct event* e)
{
	if(((unsigned int)-1) != e->min_heap_idx)
	{
		struct event *last = s->p[--s->n];
		unsigned parent = (e->min_heap_idx - 1) / 2;
		if (e->min_heap_idx > 0 && min_heap_elem_greater(s->p[parent], last))
		{
			min_heap_shift_up_(s, e->min_heap_idx, last);
		}
		else
		{
			min_heap_shift_down_(s, e->min_heap_idx, last);
		}
		e->min_heap_idx = -1;
		return 0;
	}
	return -1;
}
