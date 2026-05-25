#!/bin/bash
set -e

echo "=== Nomalloc Development Environment Setup ==="
echo ""

echo "Checking prerequisites..."

check_command() {
    if command -v "$1" &> /dev/null; then
        echo "  ✓ $1 installed"
        return 0
    else
        echo "  ✗ $1 not found (required)"
        return 1
    fi
}

check_optional_command() {
    if command -v "$1" &> /dev/null; then
        echo "  ✓ $1 installed"
    else
        echo "  - $1 not found (optional)"
    fi
}

REQUIRED_COMMANDS=("gcc" "cmake" "make")
OPTIONAL_COMMANDS=("clang" "valgrind" "perf" "gdb")

MISSING=0

for cmd in "${REQUIRED_COMMANDS[@]}"; do
    if ! check_command "$cmd"; then
        MISSING=1
    fi
done

for cmd in "${OPTIONAL_COMMANDS[@]}"; do
    check_optional_command "$cmd"
done

echo ""
echo "Checking libraries..."

if pkg-config --exists numa 2>/dev/null; then
    echo "  ✓ NUMA library found"
else
    echo "  - NUMA library not found (optional, for NUMA support)"
fi

echo ""
if [ $MISSING -eq 1 ]; then
    echo "Some required tools are missing. Please install them:"
    echo ""
    echo "On Ubuntu/Debian:"
    echo "  sudo apt-get install build-essential cmake"
    echo ""
    echo "On CentOS/RHEL:"
    echo "  sudo yum groupinstall 'Development Tools'"
    echo "  sudo yum install cmake"
    echo ""
    echo "On Fedora:"
    echo "  sudo dnf groupinstall 'Development Tools'"
    echo "  sudo dnf install cmake"
    echo ""
    echo "For NUMA support (optional):"
    echo "  sudo apt-get install libnuma-dev  # Ubuntu/Debian"
    echo "  sudo yum install numactl-devel    # CentOS/RHEL"
    exit 1
fi

echo "All prerequisites satisfied!"
echo ""
echo "To build the project:"
echo "  ./scripts/build/build.sh"
echo ""