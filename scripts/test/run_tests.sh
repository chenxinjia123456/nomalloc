#!/bin/bash
set -e

NOMALLOC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${NOMALLOC_DIR}/build"

echo "=== Nomalloc Test Script ==="
echo ""

if [ ! -d "${BUILD_DIR}" ]; then
    echo "Build directory not found. Please run build.sh first."
    exit 1
fi

cd "${BUILD_DIR}"

echo "Step 1: Running CTest..."
ctest --output-on-failure -j$(nproc)

echo ""
echo "Step 2: Running unit tests directly..."
./tests/unit/unit_tests

echo ""
echo "Step 3: Running single-thread benchmark..."
echo "---"
./tests/benchmark/single_thread_bench

echo ""
echo "Step 4: Running multi-thread benchmark..."
echo "---"
./tests/benchmark/multi_thread_bench

echo ""
echo "=== All tests completed! ==="