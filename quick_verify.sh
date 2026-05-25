#!/bin/bash
# Nomalloc Quick Verification (assumes gcc/cmake are installed)
# Run this in WSL after installing build-essential and cmake

PROJECT_DIR="/mnt/e/001_code/005_memcode/nomalloc"
cd "$PROJECT_DIR" || exit 1

echo "========================================"
echo "  Nomalloc Quick Build Test"
echo "========================================"
echo ""

# Check tools
echo "Checking tools:"
command -v gcc >/dev/null && echo "[OK] gcc: $(gcc --version | head -1)" || { echo "[MISSING] gcc - Run: sudo apt install build-essential"; exit 1; }
command -v cmake >/dev/null && echo "[OK] cmake: $(cmake --version | head -1)" || { echo "[MISSING] cmake - Run: sudo apt install cmake"; exit 1; }
echo ""

# Build
echo "Building..."
rm -rf build
mkdir -p build

cmake -B build -DCMAKE_BUILD_TYPE=Release -DNOMALLOC_ENABLE_TESTS=ON -DNOMALLOC_ENABLE_NUMA=OFF -DNOMALLOC_ENABLE_HUGEPAGES=OFF

NPROC=$(nproc 2>/dev/null || echo 4)
cmake --build build -j$NPROC

echo ""

# Check result
if [ -f build/libnomalloc.a ]; then
    echo "[SUCCESS] libnomalloc.a built"
    ls -lh build/libnomalloc.a
fi

if [ -f build/libnomalloc.so ]; then
    echo "[SUCCESS] libnomalloc.so built"
    ls -lh build/libnomalloc.so
fi

echo ""

# Run example
if [ -f build/examples/basic/hello_nomalloc ]; then
    echo "Running example..."
    ./build/examples/basic/hello_nomalloc
fi

echo ""
echo "=== Build Verification Complete ==="