#!/bin/bash
# 最小测试脚本 - 仅验证核心功能

cd /mnt/e/001_code/005_memcode/nomalloc 2>/dev/null || {
    echo "Cannot find project directory"
    exit 1
}

echo "=== Minimal Build Test ==="
echo "PWD: $(pwd)"
echo ""

# 检查关键文件
echo "Checking files:"
test -f "src/cache/lirs/lirs.h" && echo "[OK] lirs.h" || echo "[MISSING] lirs.h"
test -f "src/gc/gc.c" && echo "[OK] gc.c" || echo "[MISSING] gc.c"
test -f "src/os/numa.c" && echo "[OK] numa.c" || echo "[MISSING] numa.c"

# 检查工具
echo ""
echo "Checking tools:"
which gcc >/dev/null && echo "[OK] gcc" || echo "[MISSING] gcc"
which cmake >/dev/null && echo "[OK] cmake" || echo "[MISSING] cmake"

# 尝试编译单个文件
echo ""
echo "Test compile:"
echo 'int main() { return 0; }' > /tmp/test.c
gcc /tmp/test.c -o /tmp/test && echo "[OK] GCC works" || echo "[FAIL] GCC error"
rm -f /tmp/test /tmp/test.c

echo ""
echo "=== Test Complete ==="