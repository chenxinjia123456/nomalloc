# Nomalloc - API 文档

## 核心 API

### 初始化和关闭

```c
#include <nomalloc.h>

// 初始化分配器（可选，首次 malloc 时自动初始化）
int nomalloc_init(void);

// 关闭分配器
void nomalloc_shutdown(void);

// 检查是否已初始化
bool nomalloc_is_initialized(void);

// 配置分配器
int nomalloc_configure(const struct nomalloc_allocator_config* config);

// 打印统计信息
void nomalloc_print_stats(void);
```

### POSIX 内存分配 API

```c
#include <nomalloc/posix_api.h>

// 标准内存分配
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);

// 对齐分配
void* aligned_alloc(size_t alignment, size_t size);
int posix_memalign(void** memptr, size_t alignment, size_t size);
void* valloc(size_t size);

// 内存信息
size_t malloc_usable_size(void* ptr);
```

### Size Class API

```c
#include <nomalloc/types.h>

// Size Class 映射
size_t size_to_class(size_t size);
size_t class_to_size(size_t class_idx);

// Size Class 类型判断
bool is_small_class(size_t size);
bool is_medium_class(size_t size);
bool is_large_class(size_t size);

// Size Class 对齐
size_t size_class_align(size_t size);

// 常量
#define NOMALLOC_SMALL_SIZE_MAX  (4 * 1024)      // 4KB
#define NOMALLOC_MEDIUM_SIZE_MAX (1 * 1024 * 1024) // 1MB
#define NOMALLOC_NUM_SIZE_CLASSES_SMALL  30
#define NOMALLOC_NUM_SIZE_CLASSES_MEDIUM 17
#define NOMALLOC_NUM_SIZE_CLASSES_TOTAL  47
```

## 垃圾回收 API

### GC 控制

```c
#include <nomalloc/gc_api.h>

// 同步触发 GC
int gc_collect(void);

// 异步触发 GC
int gc_collect_async(void);
int gc_collect_wait(void);

// 分代 GC
int gc_collect_young(void);   // Young Gen GC
int gc_collect_mixed(void);   // Mixed GC
int gc_collect_full(void);    // Full GC

// GC 启用/禁用
int gc_enable(void);
int gc_disable(void);
bool gc_is_enabled(void);

// GC 配置
int gc_set_threshold(size_t threshold);
size_t gc_get_threshold(void);

int gc_set_watermark(double high, double low);
void gc_get_watermark(double* high, double* low);

int gc_set_region_size(size_t size);
size_t gc_get_region_size(void);

int gc_set_pause_target(uint64_t target_ms);
uint64_t gc_get_pause_target(void);

int gc_set_generation_threshold(uint8_t generation, size_t threshold);
int gc_set_mixed_gc_ratio(double ratio);

// GC 统计
int gc_get_stats(struct nomalloc_gc_stats* stats);
int gc_reset_stats(void);
void gc_print_stats(void);

// GC 状态查询
const char* gc_state_name(int state);
const char* gc_phase_name(int phase);

// GC 状态常量
#define GC_STATE_IDLE     0
#define GC_STATE_MARKING  1
#define GC_STATE_COMPACTING 2
#define GC_STATE_CLEANUP  3

#define GC_PHASE_YOUNG    0
#define GC_PHASE_MIXED    1
#define GC_PHASE_FULL     2
```

### GC 写屏障

```c
// SATB 写屏障
void gc_write_barrier(void* ptr);
void gc_write_barrier_pre(void* old_value, void* new_value);

// 对象注册
void gc_register_object(void* ptr, size_t size);
void gc_unregister_object(void* ptr);

// GC 触发检查
bool gc_should_collect(void);
void gc_check_threshold(void);
```

## 统计收集 API

### 全局统计

```c
#include <nomalloc/stats_api.h>

// 启用/禁用统计
int stats_enable(void);
int stats_disable(void);
bool stats_is_enabled(void);

// 获取统计信息
int stats_get(struct nomalloc_allocator_stats* stats);
int stats_reset(void);

// 打印统计
void stats_print_summary(void);
void stats_print_detailed(void);

// 导出统计
int stats_export_json(const char* filename);
int stats_export_csv(const char* filename);
int stats_export_binary(const char* filename);

// 快捷查询
uint64_t stats_get_total_allocated(void);
uint64_t stats_get_total_freed(void);
uint64_t stats_get_active_allocations(void);

uint64_t stats_get_tcache_hits(void);
uint64_t stats_get_tcache_misses(void);
double stats_get_tcache_hit_rate(void);

double stats_get_fragmentation_ratio(void);
double stats_get_memory_utilization(void);

uint64_t stats_get_alloc_ops(void);
uint64_t stats_get_free_ops(void);
double stats_get_throughput(void);

// 延迟统计
void stats_get_latency_stats(uint64_t* min, uint64_t* max, 
                              uint64_t* avg, uint64_t* p50, 
                              uint64_t* p99);

// 分级统计
int stats_get_per_class_stats(uint64_t* allocated_per_class,
                              uint64_t* freed_per_class,
                              uint64_t* active_per_class);

int stats_get_per_arena_stats(uint64_t* allocated_per_arena,
                              uint64_t* freed_per_arena,
                              uint64_t* chunks_per_arena);

int stats_get_per_thread_stats(uint64_t* allocs_per_thread,
                              uint64_t* frees_per_thread);
```

### 统计结构体

```c
struct nomalloc_allocator_stats {
    uint64_t total_allocated;
    uint64_t total_freed;
    uint64_t active_allocations;
    
    uint64_t tcache_hits;
    uint64_t tcache_misses;
    double tcache_hit_rate;
    
    uint64_t alloc_ops;
    uint64_t free_ops;
    double throughput_ops_per_sec;
    
    uint64_t alloc_latency_min;
    uint64_t alloc_latency_max;
    uint64_t alloc_latency_avg;
    uint64_t alloc_latency_p50;
    uint64_t alloc_latency_p99;
    
    double fragmentation_ratio;
    double memory_utilization;
    
    uint64_t gc_count;
    uint64_t gc_time_ns;
    uint64_t gc_bytes_freed;
    double gc_pause_avg_ms;
    double gc_pause_max_ms;
};

struct nomalloc_gc_stats {
    uint64_t gc_count;
    uint64_t gc_time_ns;
    uint64_t bytes_freed;
    uint64_t regions_freed;
    
    double pause_avg_ms;
    double pause_max_ms;
    double pause_min_ms;
    
    double throughput_impact_pct;
};
```

## 调试工具 API

### 泄漏检测

```c
#include <nomalloc/debug_api.h>

// 启用/禁用泄漏检测
int leak_detector_enable(void);
int leak_detector_disable(void);
bool leak_detector_is_enabled(void);

// 跟踪控制
int leak_detector_start_tracking(void);
int leak_detector_stop_tracking(void);

// 获取统计
int leak_detector_get_stats(struct nomalloc_leak_stats* stats);

// 生成报告
int leak_detector_generate_report(const char* filename);
int leak_detector_print_report(void);
int leak_detector_clear(void);

struct nomalloc_leak_stats {
    uint64_t leaked_objects;
    uint64_t leaked_bytes;
    uint64_t total_allocated;
    uint64_t total_freed;
    uint64_t active_allocations;
};
```

### 性能分析器

```c
// 启用/禁用分析器
int profiler_enable(double sample_rate);
int profiler_disable(void);
bool profiler_is_enabled(void);

// 配置采样率
int profiler_get_sample_rate(double* rate);
int profiler_set_sample_rate(double rate);

// 分析热点
int profiler_analyze_hotspots(void);
int profiler_get_top_alloc_sizes(size_t n, size_t* sizes);
int profiler_get_top_latencies(size_t n, uint64_t* latencies);
int profiler_get_top_threads(size_t n, uint64_t* thread_ids);

// 生成报告
int profiler_generate_report(const char* filename);
int profiler_print_report(void);
int profiler_clear(void);
```

### 内存追踪器

```c
// 启用/禁用追踪器
int tracer_enable(void);
int tracer_disable(void);
bool tracer_is_enabled(void);

// 获取追踪数据
int tracer_get_trace_count(uint64_t* count);
int tracer_get_recent_traces(size_t n, struct trace_entry* entries);
int tracer_clear(void);

struct trace_entry {
    uint64_t timestamp;
    uint64_t thread_id;
    int operation;
    void* address;
    size_t size;
    size_t class_idx;
    int arena_id;
    uint64_t latency_ns;
};

#define TRACE_OP_ALLOC  1
#define TRACE_OP_FREE   2
#define TRACE_OP_REALLOC 3
#define TRACE_OP_GC_START 4
#define TRACE_OP_GC_END  5
```

### 调试输出

```c
// 调试模式
void debug_set_verbose(bool verbose);
bool debug_is_verbose(void);

// 打印状态
int debug_print_allocator_state(void);
int debug_print_tcache_state(void);
int debug_print_arena_state(int arena_id);
int debug_print_gc_state(void);

// 内存验证
int debug_validate_memory(void);
int debug_check_corruption(void);

// 内存转储
int debug_dump_memory_map(const char* filename);
int debug_dump_allocation_history(const char* filename, size_t n);
```

## 配置 API

### 分配器配置

```c
#include <nomalloc/types.h>

struct nomalloc_allocator_config {
    size_t region_size;        // Region 大小（4KB-256MB）
    size_t tcache_max_size;    // tcache 最大容量
    size_t gc_threshold;       // GC 触发阈值
    double gc_watermark_high;  // GC 高水位线
    double gc_watermark_low;   // GC 低水位线
    bool numa_aware;           // NUMA 感知
    bool huge_pages;           // 大页支持
    int log_level;             // 日志级别
    bool leak_detection_enabled;
    bool stats_enabled;
    bool profiler_enabled;
    double profiler_sample_rate;
};

// 配置常量
#define NOMALLOC_REGION_SIZE_DEFAULT (2 * 1024 * 1024)
#define NOMALLOC_REGION_SIZE_MIN     (4 * 1024)
#define NOMALLOC_REGION_SIZE_MAX     (256 * 1024 * 1024)

#define NOMALLOC_TCACHE_SIZE_DEFAULT (1024 * 1024)
#define NOMALLOC_TCACHE_SIZE_MIN     (64 * 1024)

#define NOMALLOC_GC_THRESHOLD_DEFAULT (1024 * 1024 * 1024)
#define NOMALLOC_GC_WATERMARK_HIGH_DEFAULT 0.8
#define NOMALLOC_GC_WATERMARK_LOW_DEFAULT  0.6

// 日志级别
#define NOMALLOC_LOG_LEVEL_NONE  0
#define NOMALLOC_LOG_LEVEL_ERROR 1
#define NOMALLOC_LOG_LEVEL_WARN  2
#define NOMALLOC_LOG_LEVEL_INFO  3
#define NOMALLOC_LOG_LEVEL_DEBUG 4
#define NOMALLOC_LOG_LEVEL_TRACE 5
```

### NUMA 配置

```c
#include <nomalloc/numa_api.h>

// NUMA 检测
bool numa_is_available(void);
int numa_get_num_nodes(void);
int numa_get_num_cpus(void);

// NUMA 查询
int numa_get_node_for_cpu(int cpu);
int numa_get_current_node(void);
int numa_get_preferred_node(void);
int numa_set_preferred_node(int node);

// 内存信息
size_t numa_get_node_memory(int node);
size_t numa_get_node_free_memory(int node);
size_t numa_get_total_memory(void);
size_t numa_get_total_free_memory(void);

// 节点距离
int numa_get_node_distance(int node1, int node2);
int numa_get_nearest_node(int node);

// NUMA 分配
void* numa_alloc_on_node(size_t size, int node);
void* numa_alloc_local(size_t size);
void* numa_alloc_interleaved(size_t size);
void numa_free(void* ptr, size_t size);

// NUMA 绑定
int numa_bind_to_node(int node);
int numa_bind_to_cpu(int cpu);
```

### 大页配置

```c
#include <nomalloc/hugepage_api.h>

// 大页检测
bool hugepage_is_available(void);
bool hugepage_is_enabled(void);

// 大页配置
int hugepage_configure(const struct hugepage_config* config);
int hugepage_get_config(struct hugepage_config* config);

// 大页大小
size_t hugepage_get_size(void);
size_t hugepage_get_2mb_size(void);
size_t hugepage_get_1gb_size(void);

// 大页分配
void* hugepage_alloc(size_t size);
void* hugepage_alloc_2mb(size_t num_pages);
void* hugepage_alloc_1gb(size_t num_pages);
void hugepage_free(void* ptr, size_t size);

// 大页预留
int hugepage_reserve(size_t num_pages);
int hugepage_release_reserved(size_t num_pages);

// 透明大页
void hugepage_enable_transparent(void);
void hugepage_disable_transparent(void);
bool hugepage_is_transparent_enabled(void);
void* hugepage_alloc_transparent(size_t size);
void hugepage_free_transparent(void* ptr, size_t size);
```

## 错误码

```c
#define NOMALLOC_SUCCESS 0
#define NOMALLOC_ERROR_INVALID_PARAM   -1
#define NOMALLOC_ERROR_NO_MEMORY       -2
#define NOMALLOC_ERROR_NOT_INITIALIZED -3
#define NOMALLOC_ERROR_ALREADY_INITIALIZED -4
#define NOMALLOC_ERROR_THREAD_ERROR    -5
#define NOMALLOC_ERROR_LOCK_ERROR      -6
#define NOMALLOC_ERROR_GC_ERROR        -7
```

## 版本信息

```c
#define NOMALLOC_VERSION_MAJOR 0
#define NOMALLOC_VERSION_MINOR 1
#define NOMALLOC_VERSION_PATCH 0
#define NOMALLOC_VERSION "0.1.0"
```

## 示例代码

### 基础使用

```c
#include <nomalloc.h>

int main() {
    // 使用标准接口
    void* ptr1 = malloc(1024);
    void* ptr2 = calloc(10, 256);
    void* ptr3 = realloc(ptr1, 2048);
    
    free(ptr2);
    free(ptr3);
    
    // 查询内存使用
    size_t active = stats_get_active_allocations();
    printf("Active allocations: %zu bytes\n", active);
    
    return 0;
}
```

### 配置和调优

```c
#include <nomalloc.h>

int main() {
    struct nomalloc_allocator_config config = {
        .region_size = 4 * 1024 * 1024,   // 4MB Region
        .tcache_max_size = 2 * 1024 * 1024, // 2MB tcache
        .gc_threshold = 512 * 1024 * 1024,   // 512MB GC阈值
        .gc_watermark_high = 0.75,
        .gc_watermark_low = 0.50,
        .numa_aware = true,
        .huge_pages = true,
        .log_level = NOMALLOC_LOG_LEVEL_INFO,
        .stats_enabled = true
    };
    
    nomalloc_configure(&config);
    
    // 使用分配器
    void* ptr = malloc(1024);
    free(ptr);
    
    // 打印统计
    stats_print_summary();
    
    return 0;
}
```

### 泄漏检测

```c
#include <nomalloc.h>

void test_function() {
    void* ptr = malloc(1024);
    // 忘记 free(ptr) - 将被检测为泄漏
}

int main() {
    // 启用泄漏检测
    leak_detector_enable();
    leak_detector_start_tracking();
    
    test_function();
    
    // 生成泄漏报告
    leak_detector_print_report();
    leak_detector_generate_report("leaks.txt");
    
    leak_detector_disable();
    return 0;
}
```