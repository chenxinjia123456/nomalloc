#!/bin/bash
# Run this script INSIDE WSL to build and test nomalloc
# ======================================================

set -e

echo "============================================"
echo "  Nomalloc Build Script for WSL"
echo "============================================"
echo ""

# Determine project directory
PROJECT_DIR="${PWD}"

# If running from Windows path, convert to WSL path
if [[ "$PROJECT_DIR" =~ ^[A-Z]: ]]; then
    echo "Converting Windows path to WSL path..."
    DRIVE=$(echo "$PROJECT_DIR" | cut -c1 | tr '[:upper:]' '[:lower:]')
    REST=$(echo "$PROJECT_DIR" | cut -c3-)
    PROJECT_DIR="/mnt/$DRIVE$REST"
    cd "$PROJECT_DIR"
fi

echo "Project directory: $PROJECT_DIR"
echo "Current directory: $(pwd)"
echo ""

# Check prerequisites
echo "Checking prerequisites..."
MISSING=0

for cmd in gcc cmake make; do
    if ! command -v $cmd &> /dev/null; then
        echo "  [MISSING] $cmd - Please install:"
        echo "    sudo apt install -y build-essential cmake"
        MISSING=1
    else
        echo "  [OK] $cmd"
    fi
done

if [[ $MISSING -eq 1 ]]; then
    echo ""
    echo "Install missing tools first:"
    echo "  sudo apt update"
    echo "  sudo apt install -y build-essential cmake"
    exit 1
fi
echo ""

# Create build directory
BUILD_DIR="build"
echo "Creating build directory..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
echo ""

# Configure project
echo "Configuring project..."
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DNOMALLOC_ENABLE_TESTS=ON \
    -DNOMALLOC_ENABLE_DEBUG=ON \
    -DNOMALLOC_ENABLE_NUMA=OFF \
    -DNOMALLOC_ENABLE_HUGEPAGES=OFF \
    || { echo "Configuration failed"; exit 1; }
echo ""

# Build project
echo "Building project..."
NPROC=$(nproc 2>/dev/null || echo 4)
cmake --build "$BUILD_DIR" -j$NPROC \
    || { echo "Build failed"; exit 1; }
echo ""

echo "============================================"
echo "  Build Complete!"
echo "============================================"
echo ""

# Run tests
echo "Running tests..."
echo ""

# Unit tests
if [[ -f "$BUILD_DIR/tests/unit/unit_tests" ]]; then
    echo "--- Unit Tests ---"
    "$BUILD_DIR/tests/unit/unit_tests" || echo "Some tests failed"
    echo ""
fi

# Benchmarks
if [[ -f "$BUILD_DIR/tests/benchmark/single_thread_bench" ]]; then
    echo "--- Single-Thread Benchmark ---"
    "$BUILD_DIR/tests/benchmark/single_thread_bench" || echo "Benchmark failed"
    echo ""
fi

if [[ -f "$BUILD_DIR/tests/benchmark/multi_thread_bench" ]]; then
    echo "--- Multi-Thread Benchmark ---"
    "$BUILD_DIR/tests/benchmark/multi_thread_bench" || echo "Benchmark failed"
    echo ""
fi

# Example
if [[ -f "$BUILD_DIR/examples/basic/hello_nomalloc" ]]; then
    echo "--- Example ---"
    "$BUILD_DIR/examples/basic/hello_nomalloc" || echo "Example failed"
    echo ""
fi

echo "============================================"
echo "  All Tests Completed!"
echo "============================================"
echo ""

# Summary
echo "Artifacts created:"
echo "  Static library:  $BUILD_DIR/libnomalloc.a"
if [[ -f "$BUILD_DIR/libnomalloc.so" ]]; then
    echo "  Shared library:  $BUILD_DIR/libnomalloc.so"
fi
echo ""

echo "To use:"
echo "  gcc -o app app.c -L$BUILD_DIR -lnomalloc -lpthread"
echo ""
echo "To install system-wide:"
echo "  sudo cmake --install $BUILD_DIR --prefix /usr/local"
echo ""