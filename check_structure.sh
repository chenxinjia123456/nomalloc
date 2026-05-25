#!/bin/bash
# 快速检查脚本 - 检查代码结构和依赖

echo "=== Nomalloc Code Structure Check ==="
echo ""

# 检查新增的源文件
echo "Checking new source files..."

check_file() {
    if [ -f "$1" ]; then
        lines=$(wc -l < "$1")
        echo "[OK] $1 ($lines lines)"
        return 0
    else
        echo "[MISSING] $1"
        return 1
    fi
}

FAILED=0

# LIRS cache
check_file "src/cache/lirs/lirs.h" || FAILED=1
check_file "src/cache/lirs/lirs.c" || FAILED=1
check_file "src/cache/lirs/lirs_manager.h" || FAILED=1
check_file "src/cache/lirs/lirs_manager.c" || FAILED=1

# GC
check_file "src/gc/gc_types.h" || FAILED=1
check_file "src/gc/gc_region.h" || FAILED=1
check_file "src/gc/gc_region.c" || FAILED=1
check_file "src/gc/gc.h" || FAILED=1
check_file "src/gc/gc.c" || FAILED=1

# NUMA
check_file "src/os/numa.h" || FAILED=1
check_file "src/os/numa.c" || FAILED=1

# Hugepages
check_file "src/os/hugepages.h" || FAILED=1
check_file "src/os/hugepages.c" || FAILED=1

# Debug
check_file "src/debug/leak_detector.h" || FAILED=1
check_file "src/debug/leak_detector.c" || FAILED=1
check_file "src/debug/stats_collector.h" || FAILED=1
check_file "src/debug/stats_collector.c" || FAILED=1

# Docs
check_file "docs/api/README.md" || FAILED=1
check_file "docs/tuning_guide/README.md" || FAILED=1

# Benchmarks
check_file "tests/benchmark/comparative_bench.c" || FAILED=1
check_file "tests/benchmark/latency_bench.c" || FAILED=1
check_file "tests/benchmark/fragmentation_bench.c" || FAILED=1
check_file "tests/benchmark/gc_bench.c" || FAILED=1
check_file "tests/benchmark/numa_bench.c" || FAILED=1

echo ""

if [ $FAILED -eq 1 ]; then
    echo "[FAILED] Some files are missing"
    exit 1
fi

echo "[OK] All required files present"
echo ""

# 检查头文件依赖
echo "Checking header dependencies..."

# 检查 atomic.h 中的 atomic_ptr_t
if grep -q "atomic_ptr_t" src/utils/atomic.h; then
    echo "[OK] atomic_ptr_t defined in atomic.h"
else
    echo "[FAILED] atomic_ptr_t not defined in atomic.h"
    FAILED=1
fi

# 检查 time.h 中的函数
if grep -q "get_time_ns" src/utils/time.h; then
    echo "[OK] get_time_ns defined in time.h"
else
    echo "[FAILED] get_time_ns not defined in time.h"
    FAILED=1
fi

echo ""

# 统计代码行数
echo "Code statistics:"
echo "  LIRS cache: $(wc -l src/cache/lirs/*.h src/cache/lirs/*.c 2>/dev/null | tail -1)"
echo "  GC: $(wc -l src/gc/*.h src/gc/*.c 2>/dev/null | tail -1)"
echo "  NUMA: $(wc -l src/os/numa.h src/os/numa.c 2>/dev/null | tail -1)"
echo "  Hugepages: $(wc -l src/os/hugepages.h src/os/hugepages.c 2>/dev/null | tail -1)"
echo "  Debug: $(wc -l src/debug/*.h src/debug/*.c 2>/dev/null | tail -1)"
echo "  Benchmarks: $(wc -l tests/benchmark/*.c 2>/dev/null | tail -1)"
echo ""

echo "=== Structure Check Complete ==="

if [ $FAILED -eq 0 ]; then
    echo "[OK] All checks passed"
    echo ""
    echo "To build, run:"
    echo "  ./verify_build.sh"
else
    echo "[FAILED] Some checks failed"
    exit 1
fi