#ifndef _MINHEAP_HPP_
#define _MINHEAP_HPP_

#ifdef __cplusplus
extern "C" {
#endif


typedef struct min_heap min_heap_t;
struct event;
struct min_heap
{
	struct event** p;		
	unsigned n;
	unsigned a;
};



void min_heap_dtor(min_heap_t* s);
void min_heap_ctor(min_heap_t* s);
void min_heap_elem_init(struct event* e);
int min_heap_elem_greater(struct event* a, struct event* b);
int min_heap_empty(min_heap_t* s);
unsigned min_heap_size(min_heap_t* s);
struct event*  min_heap_top(min_heap_t* s);
int min_heap_reserve(min_heap_t* s, unsigned n);
void min_heap_shift_up_(min_heap_t* s, unsigned hole_index, struct event* e);
void min_heap_shift_down_(min_heap_t* s, unsigned hole_index, struct event* e);
int min_heap_push(min_heap_t* s, struct event* e);
struct event* min_heap_pop(min_heap_t* s);
int min_heap_erase(min_heap_t* s, struct event* e);



#ifdef __cplusplus
}
#endif


#endif
