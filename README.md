# Nomalloc - Novel Optimized Memory Allocator

## 项目简介

Nomalloc 是一个高性能通用内存分配器，专为现代多核处理器和 NUMA 系统设计。

### 核心特性

- **高性能 Thread-Local Cache (tcache)**: 无锁设计，自适应容量调整
- **双层 LIRS 缓存淘汰**: tcache + arena 双层缓存管理，提升内存复用率
- **增量并发 GC**: G1 风格垃圾回收，暂停时间 < 100ms
- **NUMA 感知**: 多 NUMA 节点优化，本地内存优先分配
- **Region 可配置**: 支持 4KB-256MB 可配置 Region 大小
- **大页支持**: 透明大页和显式大页优化
- **跨架构支持**: x86_64 和 ARM64 架构优化
- **完整调试工具链**: 泄漏检测、统计收集、性能分析、可视化

### 性能目标

| 指标 | 目标 |
|------|------|
| 吞吐量 | >= jemalloc |
| P99 延迟 | <= jemalloc (< 500ns) |
| GC 暂停 | < 100ms |
| 内存碎片率 | < 15% |

### 支持平台

- Linux x86_64
- Linux ARM64

## 快速开始

### 编译

```bash
cd nomalloc
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 安装

```bash
cmake --install build --prefix /usr/local
```

### 使用

```c
#include <nomalloc.h>

int main() {
    void* ptr = malloc(1024);
    free(ptr);
    return 0;
}
```

### 链接

```bash
gcc -o myapp myapp.c -lnomalloc
```

## 文档

- [API 文档](docs/api/README.md)
- [架构文档](docs/architecture/README.md)
- [用户手册](docs/user_manual/README.md)
- [调优指南](docs/tuning_guide/README.md)

## 项目结构

```
nomalloc/
├── include/nomalloc/    # 公共头文件
├── src/                 # 源代码
│   ├── api/             # API 层
│   ├── core/            # 核心分配层
│   ├── cache/           # 缓存管理层
│   ├── memory/          # 内存管理层
│   ├── gc/              # 垃圾回收层
│   ├── os/              # 操作系统接口层
│   ├── utils/           # 工具模块
│   ├── debug/           # 调试工具模块
│   └── config/          # 配置模块
├── tests/               # 测试代码
├── docs/                # 文档
├── examples/            # 示例代码
├── scripts/             # 辅助脚本
└── tools/               # 独立工具程序
```

## 开发状态

当前版本: 0.1.0 (开发阶段)

- [x] 项目结构搭建
- [x] 基础工具模块
- [ ] 核心分配框架
- [ ] Thread-Local Cache
- [ ] LIRS 缓存淘汰
- [ ] 垃圾回收系统
- [ ] 性能优化
- [ ] 完整测试
- [ ] 文档完善

## 许可证

Apache License 2.0

## 贡献指南

参见 [CONTRIBUTING.md](CONTRIBUTING.md)

## 联系方式

项目主页: https://github.com/xxx/nomalloc