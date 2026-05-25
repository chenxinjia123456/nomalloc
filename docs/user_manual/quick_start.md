# Nomalloc - 用户快速入门指南

## 简介

Nomalloc 是一个高性能通用内存分配器，专为现代多核处理器和 NUMA 系统设计。

## 快速开始

### 1. 编译和安装

```bash
cd nomalloc
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build --prefix /usr/local
```

### 2. 基础使用

#### C 程序中使用

```c
#include <nomalloc.h>

int main() {
    // 初始化分配器（可选，首次调用 malloc 时会自动初始化）
    nomalloc_init();
    
    // 使用标准 POSIX 接口
    void* ptr = malloc(1024);
    memset(ptr, 0, 1024);
    free(ptr);
    
    // 关闭分配器（可选）
    nomalloc_shutdown();
    
    return 0;
}
```

#### 编译链接

```bash
gcc -o myapp myapp.c -lnomalloc
```

或者使用 LD_PRELOAD 替换系统 malloc：

```bash
LD_PRELOAD=/usr/local/lib/libnomalloc.so myapp
```

### 3. 高级功能

#### GC 手动触发

```c
#include <nomalloc.h>

// 同步触发 GC
gc_collect();

// 异步触发 GC
gc_collect_async();

// 配置 GC 水位线
gc_set_watermark(0.75, 0.50);
```

#### 统计信息

```c
#include <nomalloc.h>

// 打印统计摘要
nomalloc_print_stats_summary();

// 获取详细统计
struct nomalloc_allocator_stats stats;
stats_get(&stats);

// 导出 JSON 格式
stats_export_json("stats.json");
```

#### 泄漏检测

```c
#include <nomalloc.h>

// 启用泄漏检测
leak_detector_enable();

// ... 执行分配操作 ...

// 生成泄漏报告
leak_detector_generate_report("leaks.txt");
leak_detector_print_report();

// 清除泄漏检测
leak_detector_clear();
```

### 4. 配置选项

#### 环境变量配置

```bash
export NOMALLOC_REGION_SIZE=2097152      # 2MB Region
export NOMALLOC_TCACHE_SIZE=1048576      # 1MB tcache
export NOMALLOC_GC_THRESHOLD=1073741824  # 1GB GC阈值
export NOMALLOC_NUMA_AWARE=1             # NUMA感知
export NOMALLOC_HUGE_PAGES=1             # 大页支持
export NOMALLOC_LOG_LEVEL=3              # INFO级别
```

#### API 配置

```c
#include <nomalloc.h>

struct nomalloc_allocator_config config = {
    .region_size = 2 * 1024 * 1024,
    .tcache_max_size = 1024 * 1024,
    .gc_threshold = 1024 * 1024 * 1024,
    .numa_aware = true,
    .huge_pages = false,
    .log_level = NOMALLOC_LOG_LEVEL_INFO
};

allocator_configure(&config);
```

## 性能调优

### 小对象优化
- 使用 size class 对齐
- tcache 容量适当增大
- NUMA 本地分配

### 大对象优化
- 使用大页
- Region 大小匹配

### GC 调优
- 设置合适的阈值
- 调整水位线
- 手动触发时机

## 常见问题

### Q: 如何验证使用的是 nomalloc？
```bash
nm myapp | grep malloc
```
应该看到 nomalloc 的符号。

### Q: 性能不如预期？
- 检查 NUMA 配置
- 检查 tcache 命中率
- 检查 GC 触发频率

### Q: 内存泄漏检测报告？
- 使用 leak_detector_generate_report()
- 检查报告中的泄漏对象
- 查看分配位置和大小

## 更多信息

- [完整 API 文档](../api/README.md)
- [架构文档](../architecture/README.md)
- [调优指南](../tuning_guide/README.md)