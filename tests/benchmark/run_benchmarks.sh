#!/bin/bash
# Nomalloc Benchmark Runner
# Runs all benchmarks and generates comparison report

set -e

BUILD_DIR="${BUILD_DIR:-build}"
BENCHMARK_DIR="$BUILD_DIR/tests/benchmark"
RESULTS_DIR="${RESULTS_DIR:-benchmark_results}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

mkdir -p "$RESULTS_DIR"

echo "=== Nomalloc Benchmark Suite ==="
echo "Build directory: $BUILD_DIR"
echo "Results directory: $RESULTS_DIR"
echo "Timestamp: $TIMESTAMP"
echo ""

run_benchmark() {
    local name=$1
    local executable=$2
    local output_file="$RESULTS_DIR/${name}_${TIMESTAMP}.txt"
    
    echo "Running $name..."
    
    if [ -f "$BENCHMARK_DIR/$executable" ]; then
        "$BENCHMARK_DIR/$executable" > "$output_file" 2>&1
        echo "  Results saved to: $output_file"
    else
        echo "  Executable not found: $BENCHMARK_DIR/$executable"
        return 1
    fi
}

echo "=== Building Benchmarks ==="
cmake --build "$BUILD_DIR" --target all

echo ""
echo "=== Running Benchmarks ==="

run_benchmark "single_thread" "single_thread_bench"
run_benchmark "multi_thread" "multi_thread_bench"
run_benchmark "latency" "latency_bench"
run_benchmark "fragmentation" "fragmentation_bench"
run_benchmark "gc" "gc_bench"
run_benchmark "numa" "numa_bench"

if [ -f "$BENCHMARK_DIR/comparative_bench" ]; then
    run_benchmark "comparative" "comparative_bench"
fi

echo ""
echo "=== Generating Summary ==="

SUMMARY_FILE="$RESULTS_DIR/summary_${TIMESTAMP}.txt"

echo "Nomalloc Benchmark Summary - $TIMESTAMP" > "$SUMMARY_FILE"
echo "============================================" >> "$SUMMARY_FILE"

for file in "$RESULTS_DIR"/*_${TIMESTAMP}.txt; do
    if [ -f "$file" ]; then
        name=$(basename "$file" _${TIMESTAMP}.txt)
        echo "" >> "$SUMMARY_FILE"
        echo "--- $name ---" >> "$SUMMARY_FILE"
        
        grep -E "Throughput:|Avg latency:|P99 latency:|Fragmentation:|GC count:" "$file" >> "$SUMMARY_FILE" 2>/dev/null || true
    fi
done

echo "Summary saved to: $SUMMARY_FILE"

echo ""
echo "=== Benchmark Complete ==="
echo "Results saved in: $RESULTS_DIR"
echo ""

if [ -f "$SUMMARY_FILE" ]; then
    echo "=== Quick Summary ==="
    cat "$SUMMARY_FILE"
fi