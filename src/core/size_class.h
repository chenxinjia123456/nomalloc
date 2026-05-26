/**
 * @file size_class.h
 * @brief Nomalloc尺寸分类系统
 * 
 * 本文件定义了nomalloc的内存尺寸分类策略，是核心优化机制之一。
 * 
 * 尺寸分类原理：
 * 
 * 传统内存分配器面临的问题：
 * - 不同大小的对象混杂，导致内存碎片
 * - 难以高效复用已释放的内存块
 * - 对齐要求增加分配开销
 * 
 * 尺寸分类解决方案：
 * - 将请求大小映射到固定尺寸类别
 * - 同类对象从同一缓存池分配
 * - 减少碎片，提高复用率
 * 
 * nomalloc采用三层分类：
 * 
 * ┌───────────────────────────────────────────────┐
 * │ 小对象层 (Small Size Class)                   │
 * │  8B ~ 4KB (30个类别)                          │
 * │  特点：高频分配，线程缓存优化                  │
 * │  示例：8, 16, 32, 48, 64, ... 4096           │
 * ├───────────────────────────────────────────────┤
 * │ 中对象层 (Medium Size Class)                  │
 * │  4KB ~ 1MB (17个类别)                         │
 * │  特点：中等频率，arena管理                     │
 * │  示例：8KB, 12KB, 16KB, ... 1MB              │
 * ├───────────────────────────────────────────────┤
 * │ 大对象层 (Large Size Class)                   │
 * │  > 1MB                                       │
 * │  特点：低频分配，直接系统调用                  │
 * │  策略：mmap直接分配，无缓存                    │
 * └───────────────────────────────────────────────┘
 * 
 * 尺寸分布设计：
 * - 小对象：密集分布（满足高频小分配）
 * - 中对象：稀疏分布（平衡覆盖和效率）
 * - 大对象：无分类（直接系统分配）
 * 
 * 性能优势：
 * - 小对象命中率高（覆盖90%+的分配）
 * - 减少锁竞争（同类对象共享缓存）
 * - 降低碎片（同类对象完美复用）
 * 
 * @author chenxinjia123456
 * @date 2026
 */

#ifndef NOMALLOC_CORE_SIZE_CLASS_H
#define NOMALLOC_CORE_SIZE_CLASS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <nomalloc/types.h>
#include "../utils/math.h"
#include "../utils/memory.h"
#include "../utils/assert.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup SizeClassConstants 尺寸分类常量
 * @brief 定义尺寸分类的数量和边界
 * @{
 */

/**
 * @def NUM_SMALL_CLASSES
 * @brief 小对象尺寸类别数量
 * 
 * 覆盖8B~4KB范围，共30个类别。
 * 设计为高频分配优化，提供密集分类。
 */
#ifndef NUM_SMALL_CLASSES
#define NUM_SMALL_CLASSES NOMALLOC_NUM_SIZE_CLASSES_SMALL
#endif

/**
 * @def NUM_MEDIUM_CLASSES
 * @brief 中对象尺寸类别数量
 * 
 * 覆盖4KB~1MB范围，共17个类别。
 * 设计为中等频率分配，稀疏分类节省管理开销。
 */
#ifndef NUM_MEDIUM_CLASSES
#define NUM_MEDIUM_CLASSES NOMALLOC_NUM_SIZE_CLASSES_MEDIUM
#endif

/**
 * @def NUM_SIZE_CLASSES
 * @brief 总尺寸类别数量
 * 
 * = NUM_SMALL_CLASSES + NUM_MEDIUM_CLASSES
 * 用于数组定义和循环边界。
 */
#ifndef NUM_SIZE_CLASSES
#define NUM_SIZE_CLASSES NOMALLOC_NUM_SIZE_CLASSES_TOTAL
#endif

/**
 * @def SIZE_CLASS_MIN
 * @brief 最小分配大小
 * 
 * 8字节，满足最小对齐要求。
 * 所有小于8的请求都会调整为8。
 */
#define SIZE_CLASS_MIN 8

/**
 * @def SIZE_CLASS_SMALL_MAX
 * @brief 小对象最大尺寸
 * 
 * 4096字节（4KB），与页大小对齐。
 * 超过此值进入中对象分类。
 */
#define SIZE_CLASS_SMALL_MAX NOMALLOC_SMALL_SIZE_MAX

/**
 * @def SIZE_CLASS_MEDIUM_MAX
 * @brief 中对象最大尺寸
 * 
 * 1048576字节（1MB），超过此值为大对象。
 * 大对象直接系统分配，无缓存。
 */
#define SIZE_CLASS_MEDIUM_MAX NOMALLOC_MEDIUM_SIZE_MAX

/**
 * @def SIZE_CLASS_LARGE_THRESHOLD
 * @brief 大对象阈值
 * 
 * 等于SIZE_CLASS_MEDIUM_MAX。
 * 用于快速判断大对象分类。
 */
#define SIZE_CLASS_LARGE_THRESHOLD SIZE_CLASS_MEDIUM_MAX

/** @} */

/**
 * @defgroup SizeClassTables 尺寸类别表
 * @brief 预定义的尺寸类别数组
 * 
 * 尺寸类别设计原则：
 * 1. 覆盖常用大小（避免浪费）
 * 2. 合理间距（平衡碎片和效率）
 * 3. 对齐友好（减少额外开销）
 * 
 * 小对象分布：
 * - 8~128B: 密集分布（每16B一个类别）
 * - 128~512B: 中等分布（每64B一个类别）
 * - 512~4096B: 稀疏分布（按几何级数）
 * 
 * 中对象分布：
 * - 4KB~64KB: 按几何级数增长
 * - 64KB~1MB: 更大间距
 * @{
 */

/**
 * @brief 小对象尺寸类别表
 * 
 * 30个类别，覆盖8B~4KB。
 * 
 * 分类示例：
 * - 请求10B → 分配16B（类别1）
 * - 请求50B → 分配64B（类别4）
 * - 请求100B → 分配128B（类别8）
 * - 请求500B → 分配512B（类别16）
 * 
 * 浪费率控制：
 * - 平均浪费率 ~10-20%
 * - 小对象浪费相对可接受
 */
static const size_t small_size_classes[NUM_SMALL_CLASSES] = {
    8, 16, 32, 48, 64, 80, 96, 112, 128,
    160, 192, 224, 256, 320, 384, 448, 512,
    640, 768, 896, 1024, 1280, 1536, 1792, 2048,
    2560, 3072, 3584, 4096
};

/**
 * @brief 中对象尺寸类别表
 * 
 * 17个类别，覆盖4KB~1MB。
 * 
 * 分类示例：
 * - 请求5KB → 分配8KB（类别0）
 * - 请求20KB → 分配20KB（类别3）
 * - 请求100KB → 分配131KB（类别10）
 * 
 * 策略：
 * - 采用几何级数增长
 * - 覆盖常用中等大小
 * - 大间距降低管理开销
 */
static const size_t medium_size_classes[NUM_MEDIUM_CLASSES] = {
    8192, 12288, 16384, 20480, 24576, 28672, 32768,
    49152, 65536, 98304, 131072, 196608, 262144,
    393216, 524288, 786432, 1048576
};

/** @} */

/**
 * @defgroup SizeClassFunctions 尺寸分类查询函数
 * @brief 大小与类别索引的双向映射
 * @{
 */

/**
 * @brief 小对象大小转换为类别索引
 * 
 * 线性搜索找到首个满足的类别。
 * 
 * 算法：
 * for i in 0..NUM_SMALL_CLASSES:
 *     if size <= small_size_classes[i]:
 *         return i
 * 
 * 性能：
 * - O(NUM_SMALL_CLASSES) = O(30)
 * - 小对象分类足够快
 * - 可优化为二分查找（但收益不大）
 * 
 * @param size 请求大小（必须<=SIZE_CLASS_SMALL_MAX）
 * @return 类别索引（0~NUM_SMALL_CLASSES-1）
 * 
 * @note 断言验证size范围
 */
static inline size_t size_to_class_small(size_t size) {
    nomalloc_assert(size <= SIZE_CLASS_SMALL_MAX, "size too large for small class");
    
    if (size <= 8) return 0;
    
    for (size_t i = 0; i < NUM_SMALL_CLASSES; i++) {
        if (size <= small_size_classes[i]) {
            return i;
        }
    }
    
    return NUM_SMALL_CLASSES - 1;
}

/**
 * @brief 中对象大小转换为类别索引
 * 
 * 类似小对象，但返回全局索引（含小对象偏移）。
 * 
 * @param size 请求大小（必须在中对象范围）
 * @return 全局类别索引（NUM_SMALL_CLASSES ~ NUM_SIZE_CLASSES-1）
 * 
 * @note 返回值需加上NUM_SMALL_CLASSES偏移
 */
static inline size_t size_to_class_medium(size_t size) {
    nomalloc_assert(size > SIZE_CLASS_SMALL_MAX && size <= SIZE_CLASS_MEDIUM_MAX, 
                    "size out of medium range");
    
    size_t adjusted_size = size;
    if (adjusted_size <= SIZE_CLASS_SMALL_MAX) {
        adjusted_size = SIZE_CLASS_SMALL_MAX + 1;
    }
    
    for (size_t i = 0; i < NUM_MEDIUM_CLASSES; i++) {
        if (adjusted_size <= medium_size_classes[i]) {
            return NUM_SMALL_CLASSES + i;
        }
    }
    
    return NUM_SIZE_CLASSES - 1;
}

/**
 * @brief 任意大小转换为类别索引
 * 
 * 综合判断大小范围，调用对应分类函数。
 * 
 * 分类逻辑：
 * - size <= 4KB: size_to_class_small()
 * - 4KB < size <= 1MB: size_to_class_medium()
 * - size > 1MB: 返回NUM_SIZE_CLASSES（大对象标记）
 * 
 * @param size 请求大小（必须>0）
 * @return 类别索引（0~NUM_SIZE_CLASSES）
 * 
 * @note 大对象返回NUM_SIZE_CLASSES（超出数组范围）
 */
static inline size_t size_to_class(size_t size) {
    nomalloc_assert(size > 0, "size must be positive");
    
    if (size <= SIZE_CLASS_SMALL_MAX) {
        return size_to_class_small(size);
    } else if (size <= SIZE_CLASS_MEDIUM_MAX) {
        return size_to_class_medium(size);
    } else {
        return NUM_SIZE_CLASSES;
    }
}

/**
 * @brief 类别索引转换为实际大小
 * 
 * 从尺寸表查询实际分配大小。
 * 
 * 查询逻辑：
 * - class_idx < NUM_SMALL_CLASSES: small_size_classes[idx]
 * - NUM_SMALL_CLASSES <= idx < NUM_SIZE_CLASSES: medium_size_classes[idx-offset]
 * - idx >= NUM_SIZE_CLASSES: 0（大对象无固定大小）
 * 
 * @param class_idx 类别索引
 * @return 实际分配大小，无效返回0
 * 
 * @note 大对象返回0，表示需单独处理
 */
static inline size_t class_to_size(size_t class_idx) {
    if (class_idx < NUM_SMALL_CLASSES) {
        return small_size_classes[class_idx];
    } else if (class_idx < NUM_SIZE_CLASSES) {
        return medium_size_classes[class_idx - NUM_SMALL_CLASSES];
    } else {
        return 0;
    }
}

/** @} */

static inline bool is_small_class(size_t size) {
    return size <= SIZE_CLASS_SMALL_MAX;
}

static inline bool is_medium_class(size_t size) {
    return size > SIZE_CLASS_SMALL_MAX && size <= SIZE_CLASS_MEDIUM_MAX;
}

static inline bool is_large_class(size_t size) {
    return size > SIZE_CLASS_MEDIUM_MAX;
}

static inline bool is_small_class_idx(size_t class_idx) {
    return class_idx < NUM_SMALL_CLASSES;
}

static inline bool is_medium_class_idx(size_t class_idx) {
    return class_idx >= NUM_SMALL_CLASSES && class_idx < NUM_SIZE_CLASSES;
}

static inline bool is_large_class_idx(size_t class_idx) {
    return class_idx >= NUM_SIZE_CLASSES;
}

static inline size_t size_class_align(size_t size) {
    size_t page_size = get_page_size();
    if (size <= SIZE_CLASS_SMALL_MAX) {
        size_t class_idx = size_to_class_small(size);
        return small_size_classes[class_idx];
    } else if (size <= SIZE_CLASS_MEDIUM_MAX) {
        size_t class_idx = size_to_class_medium(size);
        return medium_size_classes[class_idx - NUM_SMALL_CLASSES];
    } else {
        return align_up(size, page_size);
    }
}

static inline size_t get_num_size_classes(void) {
    return NUM_SIZE_CLASSES;
}

static inline size_t get_num_small_classes(void) {
    return NUM_SMALL_CLASSES;
}

static inline size_t get_num_medium_classes(void) {
    return NUM_MEDIUM_CLASSES;
}

static inline size_t size_class_get_max_small(void) {
    return SIZE_CLASS_SMALL_MAX;
}

static inline size_t size_class_get_max_medium(void) {
    return SIZE_CLASS_MEDIUM_MAX;
}

static inline const size_t* get_small_size_classes(void) {
    return small_size_classes;
}

static inline const size_t* get_medium_size_classes(void) {
    return medium_size_classes;
}

void size_class_print_info(void);

#ifdef __cplusplus
}
#endif

#endif