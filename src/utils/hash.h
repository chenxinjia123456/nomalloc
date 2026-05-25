#ifndef NOMALLOC_UTILS_HASH_H
#define NOMALLOC_UTILS_HASH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t hash32_rot(uint32_t x, int k) {
    return (x << k) | (x >> (32 - k));
}

static inline uint32_t hash32_murmur3(uint32_t key) {
    key ^= key >> 16;
    key *= 0x85ebca6b;
    key ^= key >> 13;
    key *= 0xc2b2ae35;
    key ^= key >> 16;
    return key;
}

static inline uint64_t hash64_murmur3(uint64_t key) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ULL;
    key ^= key >> 33;
    return key;
}

static inline uint32_t hash32_fnv1a(const void* data, size_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

static inline uint64_t hash64_fnv1a(const void* data, size_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static inline uint32_t hash32_ptr(const void* ptr) {
    uintptr_t val = (uintptr_t)ptr;
    return hash32_murmur3((uint32_t)val);
}

static inline uint64_t hash64_ptr(const void* ptr) {
    uintptr_t val = (uintptr_t)ptr;
    return hash64_murmur3((uint64_t)val);
}

static inline uint32_t hash32_int(uint32_t val) {
    return hash32_murmur3(val);
}

static inline uint64_t hash64_int(uint64_t val) {
    return hash64_murmur3(val);
}

static inline uint32_t hash32_str(const char* str) {
    return hash32_fnv1a(str, strlen(str));
}

static inline uint64_t hash64_str(const char* str) {
    return hash64_fnv1a(str, strlen(str));
}

#define HASH_EMPTY_SLOT 0

struct hash_table32 {
    uint32_t* keys;
    void** values;
    size_t capacity;
    size_t count;
    size_t tombstones;
};

static inline int hash_table32_init(struct hash_table32* table, size_t capacity) {
    table->keys = (uint32_t*)calloc(capacity, sizeof(uint32_t));
    table->values = (void**)calloc(capacity, sizeof(void*));
    if (!table->keys || !table->values) {
        free(table->keys);
        free(table->values);
        return -1;
    }
    table->capacity = capacity;
    table->count = 0;
    table->tombstones = 0;
    return 0;
}

static inline void hash_table32_destroy(struct hash_table32* table) {
    free(table->keys);
    free(table->values);
    table->keys = NULL;
    table->values = NULL;
    table->capacity = 0;
    table->count = 0;
    table->tombstones = 0;
}

#define HASH_TOMBSTONE 1

static inline void** hash_table32_lookup(struct hash_table32* table, uint32_t key) {
    if (key == HASH_EMPTY_SLOT || key == HASH_TOMBSTONE) {
        return NULL;
    }
    
    uint32_t hash = hash32_murmur3(key);
    size_t idx = hash & (table->capacity - 1);
    
    size_t probe_count = 0;
    while (probe_count < table->capacity) {
        uint32_t slot_key = table->keys[idx];
        
        if (slot_key == key) {
            return &table->values[idx];
        }
        
        if (slot_key == HASH_EMPTY_SLOT) {
            return NULL;
        }
        
        idx = (idx + 1) & (table->capacity - 1);
        probe_count++;
    }
    
    return NULL;
}

static inline int hash_table32_insert(struct hash_table32* table, 
                                       uint32_t key, void* value) {
    if (key == HASH_EMPTY_SLOT || key == HASH_TOMBSTONE) {
        return -1;
    }
    
    if (table->count + table->tombstones >= table->capacity * 0.75) {
        return -2;
    }
    
    uint32_t hash = hash32_murmur3(key);
    size_t idx = hash & (table->capacity - 1);
    
    size_t probe_count = 0;
    while (probe_count < table->capacity) {
        uint32_t slot_key = table->keys[idx];
        
        if (slot_key == key) {
            table->values[idx] = value;
            return 0;
        }
        
        if (slot_key == HASH_EMPTY_SLOT || slot_key == HASH_TOMBSTONE) {
            if (slot_key == HASH_TOMBSTONE) {
                table->tombstones--;
            }
            table->keys[idx] = key;
            table->values[idx] = value;
            table->count++;
            return 0;
        }
        
        idx = (idx + 1) & (table->capacity - 1);
        probe_count++;
    }
    
    return -3;
}

static inline int hash_table32_remove(struct hash_table32* table, uint32_t key) {
    if (key == HASH_EMPTY_SLOT || key == HASH_TOMBSTONE) {
        return -1;
    }
    
    void** slot = hash_table32_lookup(table, key);
    if (!slot) {
        return -2;
    }
    
    size_t idx = slot - table->values;
    table->keys[idx] = HASH_TOMBSTONE;
    table->values[idx] = NULL;
    table->count--;
    table->tombstones++;
    
    return 0;
}

static inline void hash_table32_clear(struct hash_table32* table) {
    memset(table->keys, 0, table->capacity * sizeof(uint32_t));
    memset(table->values, 0, table->capacity * sizeof(void*));
    table->count = 0;
    table->tombstones = 0;
}

#ifdef __cplusplus
}
#endif

#endif