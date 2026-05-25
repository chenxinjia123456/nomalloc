#!/bin/bash
set -e

BUILD_DIR="${1:-./build}"
TEST_EXEC="${BUILD_DIR}/tests/unit/unit_tests"

if [ ! -f "${TEST_EXEC}" ]; then
    echo "Unit test executable not found"
    exit 1
fi

if ! command -v valgrind &> /dev/null; then
    echo "Valgrind not found. Please install it:"
    echo "  sudo apt-get install valgrind  # Ubuntu/Debian"
    echo "  sudo yum install valgrind      # CentOS/RHEL"
    exit 1
fi

echo "=== Running Valgrind Memory Check ==="
echo ""

valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --verbose \
         --log-file=valgrind_report.txt \
         "${TEST_EXEC}"

echo ""
echo "Valgrind report saved to: valgrind_report.txt"
echo ""

if grep -q "ERROR SUMMARY: 0 errors" valgrind_report.txt; then
    echo "✓ No memory errors detected"
else
    echo "✗ Memory errors detected. Check valgrind_report.txt for details"
fi