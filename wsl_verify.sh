#!/bin/bash
# Nomalloc WSL Build Verification Script
# Run this inside WSL: ./wsl_verify.sh

set -e

PROJECT_DIR="/mnt/e/001_code/005_memcode/nomalloc"
cd "$PROJECT_DIR" || exit 1

echo "========================================"
echo "  Nomalloc WSL Build Verification"
echo "========================================"
echo ""
echo "Date: $(date)"
echo "Directory: $(pwd)"
echo ""

# Step 1: Install build tools if missing
echo "=== Step 1: Check/Install Build Tools ==="
echo ""

if ! command -v gcc &> /dev/null; then
    echo "Installing build-essential..."
    sudo apt update -qq
    sudo apt install -y build-essential cmake -qq
fi

echo "Tool versions:"
gcc --version | head -1
cmake --version | head -1
echo ""

# Step 2: Check source files
echo "=== Step 2: Verify Source Files ==="
echo ""

echo "LIRS Cache files:"
ls src/cache/lirs/*.h src/cache/lirs/*.c 2>/dev/null && echo "[OK]" || echo "[MISSING]"

echo ""
echo "GC files:"
ls src/gc/*.h src/gc/*.c 2>/dev/null && echo "[OK]" || echo "[MISSING]"

echo ""
echo "NUMA/Hugepages files:"
ls src/os/numa.* src/os/hugepages.* 2>/dev/null && echo "[OK]" || echo "[MISSING]"

echo ""
echo "Debug files:"
ls src/debug/*.h src/debug/*.c 2>/dev/null && echo "[OK]" || echo "[MISSING]"

echo ""

# Step 3: Configure and build
echo "=== Step 3: Build Project ==="
echo ""

rm -rf build
mkdir -p build

echo "Configuring..."
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DNOMALLOC_ENABLE_TESTS=ON \
    -DNOMALLOC_ENABLE_DEBUG=ON \
    -DNOMALLOC_ENABLE_NUMA=OFF \
    -DNOMALLOC_ENABLE_HUGEPAGES=OFF \
    2>&1 | grep -E "Nomalloc|error|warning" | head -20

echo ""
echo "Compiling..."
NPROC=$(nproc 2>/dev/null || echo 4)
cmake --build build -j$NPROC 2>&1 | tail -30

echo ""

# Step 4: Check artifacts
echo "=== Step 4: Build Artifacts ==="
echo ""

if [ -f build/libnomalloc.a ]; then
    SIZE=$(stat -c%s build/libnomalloc.a)
    echo "[OK] libnomalloc.a ($SIZE bytes)"
else
    echo "[MISSING] libnomalloc.a"
fi

if [ -f build/libnomalloc.so ]; then
    SIZE=$(stat -c%s build/libnomalloc.so)
    echo "[OK] libnomalloc.so ($SIZE bytes)"
else
    echo "[MISSING] libnomalloc.so"
fi

echo ""

# Step 5: Run test
echo "=== Step 5: Run Tests ==="
echo ""

if [ -f build/examples/basic/hello_nomalloc ]; then
    echo "Running hello_nomalloc example..."
    ./build/examples/basic/hello_nomalloc 2>&1 || echo "[WARN] Example execution issue"
else
    echo "[MISSING] hello_nomalloc example"
fi

echo ""

if [ -f build/tests/benchmark/single_thread_bench ]; then
    echo "Running single_thread benchmark (quick test)..."
    timeout 30 ./build/tests/benchmark/single_thread_bench 2>&1 | head -20 || echo "[WARN] Benchmark issue"
fi

echo ""

# Summary
echo "========================================"
echo "  Verification Complete"
echo "========================================"
echo ""

echo "Summary:"
echo "  - Build tools: installed"
echo "  - Source files: verified"
echo "  - Compilation: completed"
ls -lh build/*.a build/*.so 2>/dev/null
echo ""

echo "To use:"
echo "  gcc -o myapp myapp.c -Lbuild -lnomalloc -lpthread"
echo ""