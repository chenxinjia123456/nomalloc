# Nomalloc - 性能调优指南

## 概述

本指南帮助你根据应用场景优化 Nomalloc 的性能。调优目标：
- 最大化吞吐量
- 降低分配延迟
- 减少内存碎片
- 降低 GC 暂停时间

## 调优策略总览

```
┌─────────────────────────────────────────────────────────┐
│                     调优维度                              │
├─────────────────────────────────────────────────────────┤
│  Size Class  │  tcache  │  Arena  │  GC  │  NUMA  │ 大页 │
├─────────────────────────────────────────────────────────┤
│  小对象优化   │ 快速缓存 │ 并行分配 │ 低暂停│ 本地优先│大块  │
│  中对象优化   │ 自适应   │ 负载均衡│ 增量  │ 跨节点 │透明  │
│  大对象优化   │ 淘汰策略 │ Chunk   │ 水位线│ 绑定   │预留  │
└─────────────────────────────────────────────────────────┘
```

## 1. Size Class 调优

### Size Class 概念

Nomalloc 将分配请求分类到 47 个 size class：

| 类别 | 范围 | Size Class 数量 | 特点 |
|------|------|-----------------|------|
| 小对象 | 8B - 4KB | 30 | 高频分配，tcache 优化 |
| 中对象 | 4KB - 1MB | 17 | Chunk Run 管理 |
| 大对象 | > 1MB | 直接分配 | 大页或 mmap |

### Size Class 调优建议

#### 小对象优化（< 4KB）

```c
// 建议：增加小对象 tcache 容量
struct nomalloc_allocator_config config = {
    .tcache_max_size = 2 * 1024 * 1024,  // 2MB
    // ...
};
```

**最佳实践：**
- 使用 `size_class_align()` 获取对齐大小
- 小对象优先使用 tcache
- 避免 0 大小分配（会被改为 1）

#### 中对象优化（4KB - 1MB）

```c
// 建议：配置合适的 Region 大小
config.region_size = 4 * 1024 * 1024;  // 4MB Region
```

**最佳实践：**
- Region 大小匹配常用中对象
- 中对象较少使用 tcache
- Chunk Run 复用提高效率

#### 大对象优化（> 1MB）

```c
// 建议：启用大页支持
config.huge_pages = true;
```

**最佳实践：**
- 大对象直接 mmap
- 使用大页减少 TLB 压力
- 考虑对齐到页边界

### Size Class 性能指标

```c
// 获取各 size class 统计
uint64_t allocated[47], freed[47], active[47];
stats_get_per_class_stats(allocated, freed, active);

// 分析热点 size class
for (int i = 0; i < 47; i++) {
    if (active[i] > threshold) {
        printf("热点 Size Class %d: size=%zu, active=%llu\n",
               i, class_to_size(i), active[i]);
    }
}
```

## 2. Thread-Local Cache (tcache) 调优

### tcache 参数

| 参数 | 默认值 | 范围 | 说明 |
|------|--------|------|------|
| tcache_max_size | 1MB | 64KB - 8MB | tcache 最大容量 |
| bin_capacity_small | 32 | 8 - 128 | 小对象 bin 容量 |
| bin_capacity_medium | 16 | 4 - 64 | 中对象 bin 容量 |

### tcache 命中率优化

```c
// 监控 tcache 命中率
double hit_rate = stats_get_tcache_hit_rate();

if (hit_rate < 0.5) {
    // 命中率低，增大 tcache
    config.tcache_max_size *= 2;
} else if (hit_rate > 0.9) {
    // 命中率高，可以减小
    config.tcache_max_size *= 0.8;
}
```

### tcache 自适应调整

Nomalloc 内置自适应容量调整：

```
命中率 > 90% → 容量增加 20%
命中率 < 50% → 容量减少 20%
```

可以通过环境变量调整：

```bash
export NOMALLOC_TCACHE_SIZE=2097152  # 2MB
```

### tcache 调优场景

#### 多线程高频分配

```c
// 场景：多线程大量小对象分配
config.tcache_max_size = 4 * 1024 * 1024;  // 4MB
// 每个 tcache 独立，无竞争
```

#### 单线程或少线程

```c
// 场景：少量线程，大对象为主
config.tcache_max_size = 512 * 1024;  // 512KB
// 减少内存占用
```

#### 内存敏感场景

```c
// 场景：内存受限
config.tcache_max_size = 64 * 1024;  // 最小 64KB
// 定期 GC 回收 tcache 内存
gc_set_watermark(0.6, 0.4);  // 更积极 GC
```

## 3. Arena 调优

### Arena 概念

Arena 是内存管理的核心单元：

- 每个 Arena 独立管理 Chunk
- 每个 Chunk 2MB，包含多个 Run
- Run 管理特定 size class 的对象

### Arena 数量调优

```
默认：NUMA 节点数 × 2
建议：CPU 核数 / 4（最少 4，最多 64）
```

```c
// 查看 Arena 统计
uint64_t allocs[64], frees[64], chunks[64];
stats_get_per_arena_stats(allocs, frees, chunks);
```

### Arena 负载均衡

Nomalloc 自动进行 Arena 负载均衡：

```
Thread → Thread-Local Arena
如果 Arena 满载 → 选择下一个空闲 Arena
NUMA 感知 → 优先本地 NUMA Arena
```

### Arena Chunk 配置

```c
// Chunk 大小默认 2MB
#define CHUNK_DEFAULT_SIZE (2 * 1024 * 1024)

// Region 大小影响 Chunk 分配
config.region_size = 2 * 1024 * 1024;  // 与 Chunk 对齐
```

## 4. 垃圾回收 (GC) 调优

### GC 参数

| 参数 | 默认值 | 范围 | 说明 |
|------|--------|------|------|
| gc_threshold | 1GB | 100MB - 10GB | GC 触发阈值 |
| watermark_high | 0.8 | 0.5 - 0.95 | 高水位线 |
| watermark_low | 0.6 | 0.3 - 0.8 | 低水位线 |
| pause_target | 100ms | 10ms - 500ms | 目标暂停时间 |

### GC 触发策略

```
内存使用率 ≥ watermark_high → 触发 GC
内存使用率 ≤ watermark_low → GC 完成
暂停时间 ≥ pause_target → GC 中断
```

### GC 调优场景

#### 低延迟场景

```c
// 场景：要求低 GC 暂停
gc_set_pause_target(20);  // 20ms 目标
gc_set_watermark(0.7, 0.5);  // 更早触发
gc_set_threshold(256 * 1024 * 1024);  // 更小阈值
```

#### 高吞吐场景

```c
// 场景：吞吐优先，容忍较长暂停
gc_set_pause_target(200);  // 200ms 可接受
gc_set_watermark(0.85, 0.7);  // 较晚触发
gc_set_threshold(2 * 1024 * 1024 * 1024);  // 2GB 阈值
```

#### 内存敏感场景

```c
// 场景：内存受限，需要积极回收
gc_set_watermark(0.6, 0.4);  // 更积极
gc_set_threshold(100 * 1024 * 1024);  // 100MB
// 手动定期触发
```

### 分代 GC 配置

```c
// Young Gen 配置
gc_set_generation_threshold(0, 128 * 1024 * 1024);  // 128MB

// Old Gen 配置
gc_set_generation_threshold(2, 512 * 1024 * 1024);  // 512MB

// Mixed GC 比例
gc_set_mixed_gc_ratio(0.3);  // 30% Mixed GC
```

### GC 监控

```c
struct nomalloc_gc_stats gc_stats;
gc_get_stats(&gc_stats);

printf("GC 次数: %llu\n", gc_stats.gc_count);
printf("GC 时间: %.2f ms\n", gc_stats.gc_time_ns / 1e6);
printf("平均暂停: %.2f ms\n", gc_stats.pause_avg_ms);
printf("回收字节: %llu\n", gc_stats.bytes_freed);
```

## 5. NUMA 调优

### NUMA 概念

NUMA 架构下，内存访问延迟与 CPU-内存距离相关：

```
本地内存访问延迟: ~100ns
跨节点内存访问延迟: ~200ns（增加 2 倍）
```

### NUMA 配置

```c
// 启用 NUMA 感知
config.numa_aware = true;

// 设置首选节点
numa_set_preferred_node(0);

// 启用交错分配（大数据集）
numa_set_interleave(true);
```

### NUMA 调优场景

#### 单 NUMA 节点

```c
// 无需 NUMA 优化
config.numa_aware = false;
```

#### 多 NUMA 节点 - 本地优先

```c
// 线程绑定到本地节点
int node = numa_get_current_node();
numa_bind_to_node(node);

// 本地内存分配
void* ptr = numa_alloc_local(size);
```

#### 多 NUMA 节点 - 大数据集

```c
// 大数据集交错分配
void* ptr = numa_alloc_interleaved(large_size);

// 减少 NUMA 节点间不均衡
```

### NUMA 监控

```c
// NUMA 拓扑信息
numa_print_topology();

// 各节点内存状态
for (int i = 0; i < numa_get_num_nodes(); i++) {
    size_t free = numa_get_node_free_memory(i);
    printf("Node %d free: %zu MB\n", i, free / (1024 * 1024));
}
```

## 6. 大页调优

### 大页类型

| 类型 | 大小 | 适用场景 |
|------|------|----------|
| 显式大页 | 2MB / 1GB | 大对象、固定大小 |
| 透明大页 | 2MB | 自动、无需配置 |

### 显式大页配置

```c
// 启用大页
config.huge_pages = true;

// 大页分配
void* ptr = hugepage_alloc(4 * 1024 * 1024);  // ≥ 2MB

// 预留大页
hugepage_reserve(100);  // 预留 100 个 2MB 大页
```

### 透明大页配置

```c
// 启用透明大页
hugepage_enable_transparent();

// 分配（自动使用大页）
void* ptr = hugepage_alloc_transparent(size);
```

### 大页调优场景

#### 大对象为主

```c
// 场景：大量大对象分配
config.huge_pages = true;
hugepage_reserve(50);  // 预留大页
```

#### 小对象为主

```c
// 场景：小对象为主
config.huge_pages = false;
hugepage_enable_transparent();  // 透明大页
```

#### 混合对象

```c
// 场景：混合大小
config.huge_pages = false;  // 不使用显式大页
hugepage_enable_transparent();  // 透明大页自动
```

## 7. 系统级调优

### 环境变量配置

```bash
# Region 大小
export NOMALLOC_REGION_SIZE=4194304

# tcache 大小
export NOMALLOC_TCACHE_SIZE=2097152

# GC 阈值
export NOMALLOC_GC_THRESHOLD=536870912

# NUMA 感知
export NOMALLOC_NUMA_AWARE=1

# 大页支持
export NOMALLOC_HUGE_PAGES=1

# 日志级别
export NOMALLOC_LOG_LEVEL=2
```

### Linux 内核参数

```bash
# 大页预留
echo 100 > /proc/sys/vm/nr_hugepages

# 透明大页（always/madvise/never）
echo always > /sys/kernel/mm/transparent_hugepage/enabled

# 内存过量分配
echo 1 > /proc/sys/vm/overcommit_memory

# NUMA 内存策略
numactl --interleave=all your_app
```

### CPU 亲和性

```bash
# 绑定到特定 CPU
taskset -c 0-7 your_app

# NUMA 绑定
numactl --cpunodebind=0 --membind=0 your_app
```

## 8. 性能监控

### 实时监控

```c
// 定期打印统计
while (running) {
    sleep(1);
    stats_print_summary();
}
```

### 统计导出

```c
// JSON 导出
stats_export_json("stats.json");

// CSV 导出（用于分析）
stats_export_csv("stats.csv");
```

### 关键指标监控

| 指标 | 目标值 | 监控方法 |
|------|--------|----------|
| tcache 命中率 | > 80% | `stats_get_tcache_hit_rate()` |
| 内存利用率 | > 85% | `stats_get_memory_utilization()` |
| GC 暂停 | < 100ms | `gc_stats.pause_max_ms` |
| 分配延迟 P99 | < 500ns | `stats_get_latency_stats()` |

## 9. 场景化调优

### Web 服务

```c
struct nomalloc_allocator_config config = {
    .tcache_max_size = 2 * 1024 * 1024,   // 2MB
    .gc_threshold = 256 * 1024 * 1024,    // 256MB
    .gc_watermark_high = 0.7,
    .gc_watermark_low = 0.5,
    .numa_aware = true,
    .huge_pages = false,
    .stats_enabled = true
};
```

特点：高频小对象、多线程、低延迟

### 数据库服务

```c
struct nomalloc_allocator_config config = {
    .tcache_max_size = 4 * 1024 * 1024,   // 4MB
    .gc_threshold = 1 * 1024 * 1024 * 1024, // 1GB
    .gc_watermark_high = 0.75,
    .gc_watermark_low = 0.6,
    .numa_aware = true,
    .huge_pages = true,   // 大页
    .stats_enabled = true
};
```

特点：大对象、稳定内存、高吞吐

### 实时系统

```c
struct nomalloc_allocator_config config = {
    .tcache_max_size = 1 * 1024 * 1024,   // 1MB
    .gc_threshold = 64 * 1024 * 1024,     // 64MB
    .gc_watermark_high = 0.6,
    .gc_watermark_low = 0.4,
    .numa_aware = true,
    .huge_pages = false,
    .stats_enabled = false  // 禁用统计减少开销
};

gc_set_pause_target(10);  // 10ms 目标暂停
```

特点：极低延迟、确定性内存

### 科学计算

```c
struct nomalloc_allocator_config config = {
    .tcache_max_size = 8 * 1024 * 1024,   // 8MB
    .gc_threshold = 2 * 1024 * 1024 * 1024, // 2GB
    .gc_watermark_high = 0.85,
    .gc_watermark_low = 0.7,
    .numa_aware = true,
    .huge_pages = true,   // 大页
    .stats_enabled = true
};

numa_set_interleave(true);  // NUMA 交错
```

特点：大数据集、NUMA 优化、高吞吐

## 10. 调优工作流

### 1. 基准测试

```bash
# 运行基准测试
./benchmark_nomalloc > baseline.txt
```

### 2. 分析瓶颈

```c
// 分析热点
stats_print_detailed();
profiler_analyze_hotspots();
```

### 3. 配置调优

根据分析结果调整配置。

### 4. 验证改进

```bash
# 重新测试
./benchmark_nomalloc > tuned.txt

# 对比结果
diff baseline.txt tuned.txt
```

### 5. 持续监控

```c
// 生产环境监控
stats_export_json("/var/log/nomalloc/stats.json");
```

## 11. 常见问题

### Q1: tcache 命中率低？

原因：容量不足、分配模式不匹配

解决：
```c
config.tcache_max_size *= 2;  // 增加容量
```

### Q2: GC 暂停过长？

原因：阈值过大、水位线不合理

解决：
```c
gc_set_threshold(smaller_threshold);
gc_set_watermark(0.65, 0.45);
gc_set_pause_target(50);
```

### Q3: 内存碎片率高？

原因：不合理的 size class 使用

解决：
```c
// 使用对齐分配
void* ptr = aligned_alloc(64, size);  // 缓存行对齐

// 定期 GC
gc_collect();
```

### Q4: NUMA 性能不佳？

原因：跨节点访问过多

解决：
```c
numa_bind_to_node(local_node);
config.numa_aware = true;
```

### Q5: 大页分配失败？

原因：系统预留不足

解决：
```bash
echo 100 > /proc/sys/vm/nr_hugepages
```

## 12. 最佳实践总结

1. **监控优先**: 始终启用统计，了解分配模式
2. **按场景调优**: 不同场景不同配置
3. **渐进调整**: 小步快跑，验证每次改动
4. **NUMA 感知**: 多节点系统必须启用
5. **GC 平衡**: 暂停时间和吞吐量平衡
6. **tcache 自适应**: 依赖内置自适应机制
7. **大页适量**: 大对象场景使用，避免浪费
8. **定期验证**: 生产环境持续监控