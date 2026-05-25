#!/bin/bash
set -e

echo "=== Building Stress Tests in WSL ==="

WSL_PATH="/mnt/e/001_code/005_memcode/nomalloc"

wsl bash -c "cd $WSL_PATH && mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j\$(nproc) stress_reliability stress_pressure stress_long_running"

echo ""
echo "=== Running Reliability Test ==="
wsl bash -c "cd $WSL_PATH/build && ./tests/stress/stress_reliability"

echo ""
echo "=== Running Pressure Test ==="
wsl bash -c "cd $WSL_PATH/build && timeout 60 ./tests/stress/stress_pressure || true"

echo ""
echo "=== All Tests Completed ==="