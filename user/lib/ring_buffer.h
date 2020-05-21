#ifndef USERSPACE_RINGBUFFER_H
#define USERSPACE_RINGBUFFER_H

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <rte_atomic.h>

struct ringbuffer_t
{
    void*       base_addr;      /*> Base address */
    uint32_t    entry_size;     /*> Size of each entry */
    uint32_t    capacity;       /*> Ring buffer capacity */
    uint32_t    head;           /*> Head pointer */
    uint32_t    tail;           /*> Tail pointer */
} __attribute__((__packed__));

static inline int ringbuffer_empty(struct ringbuffer_t* rb)
{
    return (rb->head == rb->tail);
}

static inline uint32_t ringbuffer_size(struct ringbuffer_t* rb)
{
    if (rb->head > rb->tail)
    {
        return (rb->capacity - rb->head) + rb->tail;
    }
    else
    {
        return (rb->tail - rb->head);
    }
}

static inline int ringbuffer_full(struct ringbuffer_t* rb)
{
    return (((rb->tail + rb->entry_size) % rb->capacity) == rb->head);
}

static inline void* ringbuffer_front(struct ringbuffer_t* rb)
{
    return (char*) rb->base_addr + rb->head;
}

static inline void* ringbuffer_back(struct ringbuffer_t* rb)
{
    return (char*) rb->base_addr + rb->tail;
}

static void ringbuffer_push(struct ringbuffer_t* rb)
{
    /* Crash if buffer is full! */
    assert(ringbuffer_full(rb));

    rte_wmb();  /* Ensure data is copied before updating pointer! */
    
    rb->tail = rb->tail + rb->entry_size;
    if (rb->tail >= rb->capacity)
        rb->tail = 0;
}

static void ringbuffer_pop(struct ringbuffer_t* rb)
{
    /* Crash if buffer is empty! */
    assert(ringbuffer_empty(rb));

    rte_wmb();  /* Ensure data is copied before updating pointer! */

    rb->head = rb->head + rb->entry_size;
    if (rb->head >= rb->capacity)
        rb->head = 0;
}

#endif /* USERSPACE_RINGBUFFER_H */
