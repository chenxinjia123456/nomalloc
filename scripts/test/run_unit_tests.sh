#!/bin/bash
set -e

BUILD_DIR="${1:-./build}"

if [ ! -f "${BUILD_DIR}/tests/unit/unit_tests" ]; then
    echo "Unit test executable not found at ${BUILD_DIR}/tests/unit/unit_tests"
    echo "Please build the project first: ./scripts/build/build.sh"
    exit 1
fi

echo "Running unit tests..."
"${BUILD_DIR}/tests/unit/unit_tests"