#include "chunk.h"
#include "../core/size_class.h"
#include "../utils/memory.h"
#include "../utils/log.h"
#include "../utils/math.h"
#include "../utils/assert.h"
#include "../os/arch/prefetch.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern void* __libc_malloc(size_t);
extern void __libc_free(void*);
extern void* __libc_calloc(size_t, size_t);

#ifndef NUM_SIZE_CLASSES
#define NUM_SIZE_CLASSES NOMALLOC_NUM_SIZE_CLASSES_TOTAL
#endif

struct chunk* chunk_create(size_t size, struct arena* arena) {
    size_t aligned_size = align_up(size, CHUNK_ALIGN);
    
    void* base = memory_alloc_aligned(aligned_size, CHUNK_ALIGN);
    if (!base) {
        log_error("Failed to allocate chunk memory: %zu bytes", aligned_size);
        return NULL;
    }
    
    size_t metadata_size = sizeof(struct chunk);
    size_t usable_offset = align_up(metadata_size, 16);
    
    struct chunk* chunk = (struct chunk*)base;
    
    chunk->base_addr = base;
    chunk->size = aligned_size;
    chunk->arena = arena;
    chunk->is_large = false;
    chunk->usable_offset = usable_offset;
    
    chunk->runs = NULL;
    chunk->num_runs = 0;
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        INIT_LIST_HEAD(&chunk->runs_free[i]);
        INIT_LIST_HEAD(&chunk->runs_full[i]);
    }
    
    spinlock_init(&chunk->lock);
    INIT_LIST_HEAD(&chunk->list);
    
    atomic64_init(&chunk->allocated_bytes, usable_offset);
    atomic64_init(&chunk->freed_bytes, 0);
    
    return chunk;
}

void chunk_destroy(struct chunk* chunk) {
    if (!chunk) return;
    
    spinlock_lock(&chunk->lock);
    
    if (chunk->runs) {
        for (size_t i = 0; i < chunk->num_runs; i++) {
            if (chunk->runs[i].alloc_bitmap) {
                __libc_free(chunk->runs[i].alloc_bitmap);
            }
        }
        __libc_free(chunk->runs);
    }
    
    spinlock_unlock(&chunk->lock);
    
    size_t chunk_size = chunk->size;
    memory_free_aligned(chunk, chunk_size, CHUNK_ALIGN);
}

struct chunk_run* chunk_find_available_run(struct chunk* chunk, size_t size_class) {
    if (!list_empty(&chunk->runs_free[size_class])) {
        return list_first_entry(&chunk->runs_free[size_class], struct chunk_run, list);
    }
    
    return NULL;
}

void* chunk_alloc_small(struct chunk* chunk, size_t size_class) {
    spinlock_lock(&chunk->lock);
    
    struct chunk_run* run = chunk_find_available_run(chunk, size_class);
    
    if (!run) {
        run = (struct chunk_run*)__libc_malloc(sizeof(struct chunk_run));
        if (!run) {
            spinlock_unlock(&chunk->lock);
            log_error("Failed to allocate chunk run metadata");
            return NULL;
        }
        
        run->size_class = size_class;
        run->block_size = class_to_size(size_class);
        if (run->block_size == 0) {
            __libc_free(run);
            spinlock_unlock(&chunk->lock);
            log_error("Invalid block size for size_class %zu", size_class);
            return NULL;
        }
        run->num_blocks = CHUNK_RUN_SIZE / run->block_size;
        
        run->bitmap_size = bitmap_size(run->num_blocks);
        run->alloc_bitmap = (bitmap_t)__libc_calloc(run->bitmap_size, sizeof(bitmap_word_t));
        
        if (!run->alloc_bitmap) {
            __libc_free(run);
            spinlock_unlock(&chunk->lock);
            log_error("Failed to allocate run bitmap");
            return NULL;
        }
        
        run->free_blocks_count = run->num_blocks;
        
        INIT_LIST_HEAD(&run->list);
        list_add_tail(&run->list, &chunk->runs_free[size_class]);
        
        chunk->num_runs++;
    }
    
    size_t block_idx = bitmap_find_first_clear(run->alloc_bitmap, run->num_blocks);
    if (block_idx >= run->num_blocks) {
        spinlock_unlock(&chunk->lock);
        log_warn("No free blocks in run for size class %zu", size_class);
        return NULL;
    }
    
    bitmap_set(run->alloc_bitmap, block_idx);
    run->free_blocks_count--;
    
    size_t run_idx = 0;
    struct chunk_run* r;
    list_for_each_entry(r, &chunk->runs_free[size_class], list) {
        if (r == run) break;
        run_idx++;
    }
    
    uintptr_t usable_base = (uintptr_t)chunk->base_addr + chunk->usable_offset;
    uintptr_t run_base = usable_base + run_idx * CHUNK_RUN_SIZE;
    void* ptr = (void*)(run_base + block_idx * run->block_size);
    
    atomic64_add_fetch(&chunk->allocated_bytes, run->block_size);
    
    if (run->free_blocks_count == 0) {
        list_del(&run->list);
        list_add_tail(&run->list, &chunk->runs_full[size_class]);
    }
    
    spinlock_unlock(&chunk->lock);
    
    prefetch_l1(ptr);
    
    return ptr;
}

void chunk_free_small(struct chunk* chunk, void* ptr, size_t size_class) {
    if (!ptr) return;
    
    spinlock_lock(&chunk->lock);
    
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t usable_base = (uintptr_t)chunk->base_addr + chunk->usable_offset;
    size_t offset = addr - usable_base;
    
    size_t run_idx = offset / CHUNK_RUN_SIZE;
    
    struct chunk_run* run = NULL;
    size_t count = 0;
    
    if (!list_empty(&chunk->runs_free[size_class])) {
        struct chunk_run* r;
        list_for_each_entry(r, &chunk->runs_free[size_class], list) {
            if (count == run_idx) {
                run = r;
                break;
            }
            count++;
        }
    }
    
    if (!run && !list_empty(&chunk->runs_full[size_class])) {
        struct chunk_run* r;
        list_for_each_entry(r, &chunk->runs_full[size_class], list) {
            if (count == run_idx) {
                run = r;
                break;
            }
            count++;
        }
    }
    
    if (!run) {
        spinlock_unlock(&chunk->lock);
        log_warn("Failed to find run for ptr %p", ptr);
        return;
    }
    
    if (run->block_size == 0) {
        spinlock_unlock(&chunk->lock);
        log_warn("Invalid block_size for ptr %p", ptr);
        return;
    }
    
    size_t block_offset = offset - run_idx * CHUNK_RUN_SIZE;
    size_t block_idx = block_offset / run->block_size;
    
    if (block_idx >= run->num_blocks) {
        spinlock_unlock(&chunk->lock);
        log_warn("Invalid block index: %zu", block_idx);
        return;
    }
    
    if (!bitmap_get(run->alloc_bitmap, block_idx)) {
        spinlock_unlock(&chunk->lock);
        log_warn("Block already free: ptr=%p, block_idx=%zu", ptr, block_idx);
        return;
    }
    
    bitmap_clear_bit(run->alloc_bitmap, block_idx);
    run->free_blocks_count++;
    
    atomic64_add_fetch(&chunk->freed_bytes, run->block_size);
    
    if (run->free_blocks_count == run->num_blocks) {
        list_del(&run->list);
        __libc_free(run->alloc_bitmap);
        __libc_free(run);
        chunk->num_runs--;
    } else if (run->free_blocks_count == 1) {
        if (list_empty(&run->list)) {
            list_add_tail(&run->list, &chunk->runs_free[size_class]);
        }
    }
    
    spinlock_unlock(&chunk->lock);
}

void* chunk_alloc_large(struct chunk* chunk, size_t size) {
    spinlock_lock(&chunk->lock);
    
    size_t aligned_size = align_up(size, CHUNK_RUN_SIZE);
    
    if (aligned_size > chunk_get_available(chunk)) {
        spinlock_unlock(&chunk->lock);
        log_warn("Not enough space in chunk for large allocation: %zu bytes", aligned_size);
        return NULL;
    }
    
    uintptr_t addr = (uintptr_t)chunk->base_addr + 
                     atomic64_load(&chunk->allocated_bytes);
    
    void* ptr = (void*)addr;
    
    atomic64_add_fetch(&chunk->allocated_bytes, aligned_size);
    chunk->is_large = true;
    
    spinlock_unlock(&chunk->lock);
    
    log_debug("Allocated large block: ptr=%p, size=%zu", ptr, aligned_size);
    return ptr;
}

void chunk_free_large(struct chunk* chunk, void* ptr, size_t size) {
    if (!ptr) return;
    
    spinlock_lock(&chunk->lock);
    
    atomic64_add_fetch(&chunk->freed_bytes, align_up(size, CHUNK_RUN_SIZE));
    
    spinlock_unlock(&chunk->lock);
    
    log_debug("Freed large block: ptr=%p, size=%zu", ptr, size);
}

void chunk_print_stats(struct chunk* chunk) {
    printf("Chunk Statistics:\n");
    printf("  Base address: %p\n", chunk->base_addr);
    printf("  Size: %zu bytes\n", chunk->size);
    printf("  Allocated: %llu bytes\n", atomic64_load(&chunk->allocated_bytes));
    printf("  Freed: %llu bytes\n", atomic64_load(&chunk->freed_bytes));
    printf("  Utilization: %.2f%%\n", chunk_get_utilization(chunk) * 100);
    printf("  Number of runs: %zu\n", chunk->num_runs);
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        size_t free_runs = list_length(&chunk->runs_free[i]);
        size_t full_runs = list_length(&chunk->runs_full[i]);
        
        if (free_runs + full_runs > 0) {
            printf("  Size class %zu: %zu free runs, %zu full runs\n",
                   i, free_runs, full_runs);
        }
    }
}