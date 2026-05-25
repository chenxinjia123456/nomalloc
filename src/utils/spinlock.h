#ifndef NOMALLOC_UTILS_SPINLOCK_H
#define NOMALLOC_UTILS_SPINLOCK_H

#include "atomic.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    atomic32_t lock;
} spinlock_t;

#define SPINLOCK_INITIALIZER { .lock = ATOMIC_VAR_INIT(0) }

static inline void spinlock_init(spinlock_t* lock) {
    atomic32_init(&lock->lock, 0);
}

static inline bool spinlock_trylock(spinlock_t* lock) {
    atomic32_t expected;
    atomic32_init(&expected, 0);
    return atomic32_compare_exchange(&lock->lock, &expected, 1);
}

static inline void spinlock_lock(spinlock_t* lock) {
    for (;;) {
        if (spinlock_trylock(lock)) {
            return;
        }
        
        while (atomic32_load(&lock->lock) == 1) {
            __asm__ volatile("pause" ::: "memory");
        }
    }
}

static inline void spinlock_unlock(spinlock_t* lock) {
    atomic32_store(&lock->lock, 0);
}

static inline bool spinlock_is_locked(spinlock_t* lock) {
    return atomic32_load(&lock->lock) != 0;
}

typedef struct {
    atomic32_t ticket;
    atomic32_t serving;
} ticket_lock_t;

#define TICKET_LOCK_INITIALIZER { \
    .ticket = ATOMIC_VAR_INIT(0), \
    .serving = ATOMIC_VAR_INIT(0) \
}

static inline void ticket_lock_init(ticket_lock_t* lock) {
    atomic32_init(&lock->ticket, 0);
    atomic32_init(&lock->serving, 0);
}

static inline void ticket_lock_lock(ticket_lock_t* lock) {
    uint32_t ticket = atomic32_inc_fetch(&lock->ticket) - 1;
    
    while (atomic32_load(&lock->serving) != ticket) {
        __asm__ volatile("pause" ::: "memory");
    }
}

static inline void ticket_lock_unlock(ticket_lock_t* lock) {
    atomic32_inc(&lock->serving);
}

typedef struct {
    atomic32_t counter;
} mcs_node_t;

typedef struct {
    atomic_ptr_t tail;
} mcs_lock_t;

static inline void mcs_lock_init(mcs_lock_t* lock) {
    atomic_init(&lock->tail, (void*)NULL);
}

static inline void mcs_lock_lock(mcs_lock_t* lock, mcs_node_t* node) {
    atomic32_init(&node->counter, 1);
    mcs_node_t* prev = atomic_exchange(&lock->tail, node);
    
    if (prev != NULL) {
        atomic32_store(&prev->counter, 0);
        while (atomic32_load(&node->counter) == 1) {
            __asm__ volatile("pause" ::: "memory");
        }
    }
}

static inline void mcs_lock_unlock(mcs_lock_t* lock, mcs_node_t* node) {
    mcs_node_t* expected = node;
    if (atomic_compare_exchange_strong(&lock->tail, &expected, NULL)) {
        return;
    }
    
    atomic32_store(&node->counter, 0);
}

#ifdef __cplusplus
}
#endif

#endif