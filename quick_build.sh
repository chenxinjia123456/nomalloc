#!/bin/bash
# Quick build and test script for nomalloc
# Run this on a Linux system

set -e

echo "=== Nomalloc Quick Build Script ==="
echo ""

# Check if we're on Linux
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo "Warning: This script is designed for Linux systems."
    echo "Current OS: $OSTYPE"
    echo ""
fi

# Check prerequisites
echo "Checking prerequisites..."
for cmd in gcc cmake make; do
    if ! command -v $cmd &> /dev/null; then
        echo "ERROR: $cmd not found. Please install:"
        echo "  Ubuntu/Debian: sudo apt-get install build-essential cmake"
        echo "  CentOS/RHEL:   sudo yum groupinstall 'Development Tools'; sudo yum install cmake"
        exit 1
    fi
    echo "  Found: $cmd"
done
echo ""

# Create build directory
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    echo "Removing existing build directory..."
    rm -rf "$BUILD_DIR"
fi

echo "Creating build directory..."
mkdir -p "$BUILD_DIR"

# Configure
echo ""
echo "Configuring project (Release mode)..."
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DNOMALLOC_ENABLE_TESTS=ON \
    -DNOMALLOC_ENABLE_DEBUG=ON

# Build
echo ""
echo "Building project..."
cmake --build "$BUILD_DIR" -j$(nproc 2>/dev/null || echo 4)

# Run tests
echo ""
echo "=== Running Tests ==="
echo ""

# Unit tests
if [ -f "$BUILD_DIR/tests/unit/unit_tests" ]; then
    echo "Running unit tests..."
    "$BUILD_DIR/tests/unit/unit_tests"
    echo ""
fi

# Single thread benchmark
if [ -f "$BUILD_DIR/tests/benchmark/single_thread_bench" ]; then
    echo "Running single-thread benchmark..."
    echo "---"
    "$BUILD_DIR/tests/benchmark/single_thread_bench"
    echo ""
fi

# Multi thread benchmark
if [ -f "$BUILD_DIR/tests/benchmark/multi_thread_bench" ]; then
    echo "Running multi-thread benchmark..."
    echo "---"
    "$BUILD_DIR/tests/benchmark/multi_thread_bench"
    echo ""
fi

# Example
if [ -f "$BUILD_DIR/examples/basic/hello_nomalloc" ]; then
    echo "Running example..."
    echo "---"
    "$BUILD_DIR/examples/basic/hello_nomalloc"
    echo ""
fi

echo "=== Build and Test Completed Successfully! ==="
echo ""
echo "Artifacts:"
echo "  Static library:  $BUILD_DIR/libnomalloc.a"
echo "  Shared library:  $BUILD_DIR/libnomalloc.so"
echo ""
echo "Usage:"
echo "  Link:   gcc -o app app.c -L$BUILD_DIR -lnomalloc -lpthread"
echo "  Preload: LD_PRELOAD=$BUILD_DIR/libnomalloc.so ./app"
echo ""