#ifndef NOMALLOC_UTILS_LIST_H
#define NOMALLOC_UTILS_LIST_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct list_head {
    struct list_head* prev;
    struct list_head* next;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

#define INIT_LIST_HEAD(ptr) do { \
    (ptr)->prev = (ptr); \
    (ptr)->next = (ptr); \
} while (0)

static inline void list_add(struct list_head* new_node, struct list_head* head) {
    head->next->prev = new_node;
    new_node->next = head->next;
    new_node->prev = head;
    head->next = new_node;
}

static inline void list_add_tail(struct list_head* new_node, struct list_head* head) {
    head->prev->next = new_node;
    new_node->prev = head->prev;
    new_node->next = head;
    head->prev = new_node;
}

static inline void list_del(struct list_head* entry) {
    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    entry->prev = NULL;
    entry->next = NULL;
}

static inline void list_del_init(struct list_head* entry) {
    list_del(entry);
    INIT_LIST_HEAD(entry);
}

static inline void list_move(struct list_head* entry, struct list_head* head) {
    list_del(entry);
    list_add(entry, head);
}

static inline void list_move_tail(struct list_head* entry, struct list_head* head) {
    list_del(entry);
    list_add_tail(entry, head);
}

static inline bool list_empty(const struct list_head* head) {
    return head->next == head;
}

static inline bool list_is_first(const struct list_head* entry, 
                                  const struct list_head* head) {
    return entry->prev == head;
}

static inline bool list_is_last(const struct list_head* entry, 
                                 const struct list_head* head) {
    return entry->next == head;
}

static inline size_t list_length(const struct list_head* head) {
    size_t count = 0;
    struct list_head* pos;
    for (pos = head->next; pos != head; pos = pos->next) {
        count++;
    }
    return count;
}

#define list_entry(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))

#define list_first_entry(head, type, member) \
    list_entry((head)->next, type, member)

#define list_last_entry(head, type, member) \
    list_entry((head)->prev, type, member)

#define list_next_entry(entry, member) \
    list_entry((entry)->member.next, typeof(*(entry)), member)

#define list_prev_entry(entry, member) \
    list_entry((entry)->member.prev, typeof(*(entry)), member)

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_prev(pos, head) \
    for (pos = (head)->prev; pos != (head); pos = pos->prev)

#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); \
         pos = n, n = pos->next)

#define list_for_each_entry(pos, head, member) \
    for (pos = list_first_entry(head, typeof(*pos), member); \
         &pos->member != (head); \
         pos = list_next_entry(pos, member))

#define list_for_each_entry_reverse(pos, head, member) \
    for (pos = list_last_entry(head, typeof(*pos), member); \
         &pos->member != (head); \
         pos = list_prev_entry(pos, member))

#define list_for_each_entry_safe(pos, n, head, member) \
    for (pos = list_first_entry(head, typeof(*pos), member), \
         n = list_next_entry(pos, member); \
         &pos->member != (head); \
         pos = n, n = list_next_entry(n, member))

struct hlist_head {
    struct hlist_node* first;
};

struct hlist_node {
    struct hlist_node* next;
    struct hlist_node** pprev;
};

#define HLIST_HEAD_INIT { .first = NULL }
#define HLIST_HEAD(name) struct hlist_head name = HLIST_HEAD_INIT
#define INIT_HLIST_HEAD(ptr) ((ptr)->first = NULL)

static inline void INIT_HLIST_NODE(struct hlist_node* node) {
    node->next = NULL;
    node->pprev = NULL;
}

static inline bool hlist_unhashed(const struct hlist_node* node) {
    return !node->pprev;
}

static inline bool hlist_empty(const struct hlist_head* head) {
    return !head->first;
}

static inline void hlist_add_head(struct hlist_node* node, struct hlist_head* head) {
    struct hlist_node* first = head->first;
    node->next = first;
    if (first) {
        first->pprev = &node->next;
    }
    head->first = node;
    node->pprev = &head->first;
}

static inline void hlist_add_before(struct hlist_node* node, struct hlist_node* next) {
    node->pprev = next->pprev;
    node->next = next;
    next->pprev = &node->next;
    *(node->pprev) = node;
}

static inline void hlist_add_behind(struct hlist_node* node, struct hlist_node* prev) {
    node->next = prev->next;
    prev->next = node;
    node->pprev = &prev->next;
    if (node->next) {
        node->next->pprev = &node->next;
    }
}

static inline void hlist_del(struct hlist_node* node) {
    struct hlist_node* next = node->next;
    struct hlist_node** pprev = node->pprev;
    *pprev = next;
    if (next) {
        next->pprev = pprev;
    }
    INIT_HLIST_NODE(node);
}

#define hlist_entry(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))

#define hlist_for_each(pos, head) \
    for (pos = (head)->first; pos; pos = pos->next)

#define hlist_for_each_safe(pos, n, head) \
    for (pos = (head)->first; pos && ({ n = pos->next; 1; }); \
         pos = n)

#define hlist_for_each_entry(pos, head, member) \
    for (pos = hlist_entry((head)->first, typeof(*pos), member); \
         pos; \
         pos = hlist_entry((pos)->member.next, typeof(*(pos)), member))

#define hlist_for_each_entry_safe(pos, n, head, member) \
    for (pos = hlist_entry((head)->first, typeof(*pos), member); \
         pos && ({ n = pos->member.next; 1; }); \
         pos = hlist_entry(n, typeof(*pos), member))

#ifdef __cplusplus
}
#endif

#endif