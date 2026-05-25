# Nomalloc 编译和测试指南

## 系统要求

### 必需软件
- GCC 7+ 或 Clang 6+ (支持 C11)
- CMake 3.15+
- Make 或 Ninja

### 可选软件
- NUMA 库（用于 NUMA 支持）
- Valgrind（用于内存检测）
- perf（用于性能分析）
- gdb（用于调试）

## 快速编译

### 1. 设置开发环境

```bash
./scripts/dev/setup_dev_env.sh
```

### 2. 编译项目

```bash
# Release 版本（优化）
./scripts/build/build.sh Release

# Debug 版本（调试）
./scripts/build/build.sh Debug
```

### 3. 运行测试

```bash
# 运行所有测试
./scripts/test/run_tests.sh

# 只运行单元测试
./scripts/test/run_unit_tests.sh

# Valgrind 内存检测
./scripts/test/valgrind_test.sh
```

## 详细编译步骤

### 手动编译

```bash
# 1. 创建构建目录
mkdir -p build && cd build

# 2. 配置项目
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DNOMALLOC_ENABLE_TESTS=ON \
    -DNOMALLOC_ENABLE_DEBUG=ON \
    -DNOMALLOC_ENABLE_NUMA=ON \
    -DNOMALLOC_ENABLE_HUGEPAGES=ON

# 3. 编译
cmake --build . -j$(nproc)

# 4. 运行测试
ctest --output-on-failure

# 5. 安装
sudo cmake --install .
```

### 编译选项

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `CMAKE_BUILD_TYPE` | Release/Debug/RelWithDebInfo | Release |
| `NOMALLOC_ENABLE_TESTS` | 启用测试 | ON |
| `NOMALLOC_ENABLE_DEBUG` | 启用调试工具 | ON |
| `NOMALLOC_ENABLE_NUMA` | 启用 NUMA 支持 | ON |
| `NOMALLOC_ENABLE_HUGEPAGES` | 启用大页支持 | ON |
| `NOMALLOC_ENABLE_SANITIZERS` | 启用 Sanitizer（Debug模式） | OFF |

## 不同平台编译

### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install build-essential cmake libnuma-dev valgrind

./scripts/build/build.sh Release
```

### CentOS/RHEL

```bash
sudo yum groupinstall 'Development Tools'
sudo yum install cmake numactl-devel valgrind

./scripts/build/build.sh Release
```

### Fedora

```bash
sudo dnf groupinstall 'Development Tools'
sudo dnf install cmake numactl-devel valgrind

./scripts/build/build.sh Release
```

### ARM64 平台

```bash
# 确保使用 ARM64 工具链
gcc --version  # 应显示 aarch64

./scripts/build/build.sh Release
```

## 测试程序

### 单元测试
```bash
./build/tests/unit/unit_tests
```

测试内容：
- 初始化/关闭
- malloc/free 基础功能
- 不同大小的分配
- calloc/realloc/aligned_alloc
- 多线程并发

### 性能基准测试
```bash
# 单线程性能
./build/tests/benchmark/single_thread_bench

# 多线程性能
./build/tests/benchmark/multi_thread_bench
```

测试内容：
- malloc/free 吞吐量
- 不同 size class 性能
- realloc 性能
- 多线程并发性能

### 压力测试
```bash
./build/tests/stress/stress_long_running
```

测试内容：
- 长时间运行稳定性
- 内存泄漏检测
- 高并发压力

## 使用方法

### 1. 链接方式

```bash
# 直接链接
gcc -o myapp myapp.c -lnomalloc

# 使用 LD_PRELOAD（无需重新编译）
LD_PRELOAD=/usr/local/lib/libnomalloc.so ./myapp
```

### 2. CMake 集成

```cmake
find_package(nomalloc REQUIRED)
target_link_libraries(myapp PRIVATE nomalloc::nomalloc)
```

### 3. 示例程序

```bash
./build/examples/basic/hello_nomalloc
```

## 调试方法

### Valgrind 检测

```bash
valgrind --leak-check=full ./build/tests/unit/unit_tests
```

### GDB 调试

```bash
gdb ./build/tests/unit/unit_tests
(gdb) run
(gdb) bt  # 查看调用栈
```

### perf 性能分析

```bash
perf record -g ./build/tests/benchmark/single_thread_bench
perf report
```

## 常见问题

### Q: 编译错误 "undefined reference to..."

检查是否正确链接所有依赖：
```bash
cmake --build build -- VERBOSE=1
```

### Q: NUMA 支持未启用

安装 NUMA 库：
```bash
sudo apt-get install libnuma-dev
```

重新配置：
```bash
cmake -B build -DNOMALLOC_ENABLE_NUMA=ON
```

### Q: 测试失败

查看详细输出：
```bash
ctest --output-on-failure -V
```

### Q: 性能不如预期

检查：
1. 是否使用 Release 版本
2. NUMA 配置是否正确
3. 大页是否启用
4. CPU 亲和性设置

## 输出目录结构

编译后目录结构：
```
build/
├── libnomalloc.a           # 静态库
├── libnomalloc.so          # 动态库
├── tests/
│   ├── unit/
│   │   └── unit_tests      # 单元测试
│   ├── benchmark/
│   │   ├── single_thread_bench
│   │   └── multi_thread_bench
│   └── stress/
│       └── stress_long_running
└── examples/
    └── basic/
        └── hello_nomalloc
```