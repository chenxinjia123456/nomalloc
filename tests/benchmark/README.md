# Nomalloc Benchmark Suite

## 概述

本基准测试套件用于评估 Nomalloc 的性能，并与 jemalloc 进行对比。

## Benchmark 类型

| Benchmark | 文件 | 说明 |
|-----------|------|------|
| 单线程测试 | `single_thread_bench.c` | 单线程吞吐量和延迟 |
| 多线程测试 | `multi_thread_bench.c` | 多线程并发性能 |
| 延迟测试 | `latency_bench.c` | 详细延迟分布分析 |
| 碎片测试 | `fragmentation_bench.c` | 内存碎片率测试 |
| GC测试 | `gc_bench.c` | GC性能和暂停时间 |
| NUMA测试 | `numa_bench.c` | NUMA感知性能 |
| 对比测试 | `comparative_bench.c` | 与jemalloc对比 |

## 编译

```bash
cd nomalloc
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 启用 jemalloc 对比

```bash
# 安装 jemalloc
sudo apt-get install libjemalloc-dev

# 编译时启用
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_JEMALLOC_BENCHMARK=ON
cmake --build build
```

## 运行

### 运行所有测试

```bash
./tests/benchmark/run_benchmarks.sh
```

### 运行单个测试

```bash
# 单线程
./build/tests/benchmark/single_thread_bench

# 多线程
./build/tests/benchmark/multi_thread_bench

# 延迟
./build/tests/benchmark/latency_bench

# 碎片
./build/tests/benchmark/fragmentation_bench

# GC
./build/tests/benchmark/gc_bench

# NUMA
./build/tests/benchmark/numa_bench

# 对比
./build/tests/benchmark/comparative_bench
```

## 输出结果

### 单线程测试输出

```
=== Nomalloc Single-Thread Benchmark ===

Benchmark: malloc/free single-thread
Iterations: 10000000

Elapsed time: 1.234 seconds
Operations: 10000000 malloc + 10000000 free
Throughput: 16.21 M ops/sec
Average latency: 61.70 ns/op

Size 16 bytes: 18.5 M ops/sec (54.05 ns/op)
Size 32 bytes: 17.8 M ops/sec (56.18 ns/op)
...
```

### 延迟测试输出

```
malloc Latency Distribution:
  Samples: 100000
  Min: 16 ns
  Max: 1024 ns
  Avg: 45.32 ns
  P50: 32 ns
  P90: 64 ns
  P95: 80 ns
  P99: 256 ns
  P99.9: 1024 ns
```

### GC测试输出

```
GC Pause Time Benchmark:
GC pause: 45.23 ms

Young GC time: 12.34 ms
Mixed GC time: 23.45 ms
Full GC time: 45.67 ms
```

### 对比测试输出

```
=== Comparison: System malloc vs jemalloc ===

Metric               jemalloc      System malloc    Diff
Throughput (ops/s)      18.5 M          15.2 M      +21.7%
Avg latency             45.2 ns         52.1 ns     -13.2%
P99 latency            256 ns          384 ns      -33.3%
```

## 性能指标

### 关键指标

| 指标 | 目标 | 说明 |
|------|------|------|
| 吞吐量 | >= 15M ops/s | malloc/free 操作速率 |
| 平均延迟 | < 100ns | 单次操作平均延迟 |
| P99延迟 | < 500ns | 99%操作延迟上限 |
| 碎片率 | < 15% | 内存碎片比例 |
| GC暂停 | < 100ms | GC最大暂停时间 |

### Size Class 性能

| Size | 目标吞吐 | 目标延迟 |
|------|----------|----------|
| 16B | 20M ops/s | 50ns |
| 64B | 18M ops/s | 55ns |
| 256B | 15M ops/s | 66ns |
| 1KB | 12M ops/s | 83ns |
| 4KB | 10M ops/s | 100ns |

## 环境配置

### 系统要求

- Linux x86_64 或 ARM64
- 至少 4GB 内存
- 多核 CPU（推荐）

### 推荐配置

```bash
# 大页预留
echo 100 > /proc/sys/vm/nr_hugepages

# NUMA 绑定（多节点系统）
numactl --cpunodebind=0 --membind=0 ./benchmark

# CPU 亲和性
taskset -c 0-7 ./benchmark
```

## 结果分析

### 吞吐量分析

- 每秒操作数（ops/sec）
- MB/s 内存带宽
- 多线程扩展性

### 延迟分析

- 平均延迟
- P50/P90/P95/P99/P99.9 分布
- 最小/最大延迟

### 碎片分析

- 碎片率变化
- GC效果
- 长期运行稳定性

### GC分析

- GC频率
- 暂停时间分布
- 分代GC效率
- 吞吐量影响

### NUMA分析

- 本地/远程访问延迟
- 节点间带宽差异
- 绑定效果

## 与 jemalloc 对比

### 测试场景

1. 小对象高频分配
2. 大对象分配
3. 多线程竞争
4. 碎片场景
5. 长期运行

### 对比维度

| 维度 | Nomalloc | jemalloc | 差异 |
|------|----------|----------|------|
| 吞吐量 | ? | ? | ? |
| 延迟P99 | ? | ? | ? |
| 碎片率 | ? | ? | ? |
| 内存占用 | ? | ? | ? |

## 自动化测试

### CI集成

```yaml
# GitHub Actions 示例
- name: Run benchmarks
  run: |
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ./tests/benchmark/run_benchmarks.sh
    
- name: Upload results
  uses: actions/upload-artifact@v2
  with:
    name: benchmark-results
    path: benchmark_results/
```

### 定期测试

```bash
# 每日运行
0 2 * * * /path/to/nomalloc/tests/benchmark/run_benchmarks.sh
```

## 结果存储

结果保存在 `benchmark_results/` 目录：

```
benchmark_results/
├── single_thread_20240101_020000.txt
├── multi_thread_20240101_020000.txt
├── latency_20240101_020000.txt
├── fragmentation_20240101_020000.txt
├── gc_20240101_020000.txt
├── numa_20240101_020000.txt
├── comparative_20240101_020000.txt
└── summary_20240101_020000.txt
```

## 调试

### 详细日志

```bash
# 启用详细日志
export NOMALLOC_LOG_LEVEL=5
./benchmark
```

### 统计导出

```bash
# JSON 格式
./benchmark && stats_export_json results.json

# CSV 格式
./benchmark && stats_export_csv results.csv
```

## 常见问题

### Q: 吞吐量低于预期？

检查：
- NUMA 配置
- tcache 命中率
- GC 频率
- CPU 核数

### Q: 延迟波动大？

检查：
- 是否有后台进程
- 系统负载
- GC 暂停

### Q: NUMA 测试失败？

确保：
- NUMA 支持
- 多节点系统
- libnuma 安装

## 参考

- [jemalloc benchmark](https://github.com/jemalloc/jemalloc/tree/master/test)
- [mimalloc benchmark](https://github.com/microsoft/mimalloc/tree/master/bench)
- [性能调优指南](../../docs/tuning_guide/README.md)