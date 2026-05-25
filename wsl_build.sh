#!/bin/bash
# Nomalloc WSL Build and Test Script
# Run: cd /mnt/e/001_code/005_memcode/nomalloc && chmod +x wsl_build.sh && ./wsl_build.sh

set -e

PROJECT_DIR="/mnt/e/001_code/005_memcode/nomalloc"
cd "$PROJECT_DIR"

echo "========================================"
echo "  Nomalloc Build Verification"
echo "========================================"
echo ""
echo "Project: $PROJECT_DIR"
echo "Date: $(date)"
echo ""

# 1. 检查文件
echo "=== Step 1: Check Source Files ==="
echo ""

FILES_OK=true

check_file() {
    if [ -f "$1" ]; then
        lines=$(wc -l < "$1" 2>/dev/null || echo "0")
        echo "[OK] $1 ($lines lines)"
    else
        echo "[MISSING] $1"
        FILES_OK=false
    fi
}

# Core files
check_file "src/cache/lirs/lirs.h"
check_file "src/cache/lirs/lirs.c"
check_file "src/cache/lirs/lirs_manager.h"
check_file "src/cache/lirs/lirs_manager.c"

check_file "src/gc/gc_types.h"
check_file "src/gc/gc_region.h"
check_file "src/gc/gc_region.c"
check_file "src/gc/gc.h"
check_file "src/gc/gc.c"

check_file "src/os/numa.h"
check_file "src/os/numa.c"
check_file "src/os/hugepages.h"
check_file "src/os/hugepages.c"

check_file "src/debug/leak_detector.h"
check_file "src/debug/leak_detector.c"
check_file "src/debug/stats_collector.h"
check_file "src/debug/stats_collector.c"

check_file "docs/api/README.md"
check_file "docs/tuning_guide/README.md"

check_file "tests/benchmark/comparative_bench.c"
check_file "tests/benchmark/latency_bench.c"
check_file "tests/benchmark/gc_bench.c"

if [ "$FILES_OK" = false ]; then
    echo ""
    echo "[ERROR] Missing files!"
    exit 1
fi

echo ""
echo "All source files present."

# 2. 检查工具
echo ""
echo "=== Step 2: Check Build Tools ==="
echo ""

for cmd in gcc cmake make; do
    if command -v $cmd >/dev/null 2>&1; then
        ver=$($cmd --version 2>/dev/null | head -1)
        echo "[OK] $cmd"
    else
        echo "[MISSING] $cmd - Install with:"
        echo "  sudo apt update && sudo apt install -y build-essential cmake"
        exit 1
    fi
done

# 3. 编译
echo ""
echo "=== Step 3: Build Project ==="
echo ""

rm -rf build
mkdir -p build

cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DNOMALLOC_ENABLE_TESTS=ON \
    -DNOMALLOC_ENABLE_NUMA=OFF \
    -DNOMALLOC_ENABLE_HUGEPAGES=OFF \
    2>&1 | head -50

echo ""
echo "Compiling..."
nproc=$(nproc 2>/dev/null || echo 4)
cmake --build build -j$nproc 2>&1 | tail -30

BUILD_RESULT=$?

if [ $BUILD_RESULT -ne 0 ]; then
    echo ""
    echo "[ERROR] Build failed!"
    echo "Check errors above"
    exit 1
fi

# 4. 检查产物
echo ""
echo "=== Step 4: Check Build Artifacts ==="
echo ""

if [ -f "build/libnomalloc.a" ]; then
    size=$(stat -c%s "build/libnomalloc.a" 2>/dev/null || echo "0")
    echo "[OK] libnomalloc.a ($size bytes)"
else
    echo "[MISSING] libnomalloc.a"
fi

if [ -f "build/libnomalloc.so" ]; then
    size=$(stat -c%s "build/libnomalloc.so" 2>/dev/null || echo "0")
    echo "[OK] libnomalloc.so ($size bytes)"
else
    echo "[MISSING] libnomalloc.so"
fi

# 5. 运行测试
echo ""
echo "=== Step 5: Run Tests ==="
echo ""

if [ -f "build/examples/basic/hello_nomalloc" ]; then
    echo "Running hello_nomalloc example..."
    ./build/examples/basic/hello_nomalloc
    echo ""
fi

if [ -f "build/tests/benchmark/single_thread_bench" ]; then
    echo "Running single-thread benchmark..."
    timeout 60 ./build/tests/benchmark/single_thread_bench 2>&1 | head -40
    echo ""
fi

# 完成
echo ""
echo "========================================"
echo "  Verification Complete"
echo "========================================"
echo ""
echo "Build successful!"
echo ""
echo "Library files:"
ls -lh build/*.a build/*.so 2>/dev/null || echo "No library files"
echo ""
echo "Usage:"
echo "  gcc -o myapp myapp.c -Lbuild -lnomalloc -lpthread"
echo ""