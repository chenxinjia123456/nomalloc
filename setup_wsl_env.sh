#!/bin/bash
# WSL Environment Setup Script for Nomalloc
# ==========================================
# Run this script inside WSL after WSL is installed

set -e

echo "============================================"
echo "  Nomalloc WSL Environment Setup"
echo "============================================"
echo ""

# Check if running in WSL
if [[ ! -f /proc/version || ! $(cat /proc/version | grep -i microsoft) ]]; then
    echo "WARNING: This script should be run inside WSL."
    echo "Current environment may not be WSL."
    echo ""
fi

echo "System Information:"
echo "  OS: $(uname -s)"
echo "  Kernel: $(uname -r)"
echo "  Arch: $(uname -m)"
echo ""

echo "Step 1: Updating system..."
sudo apt update
echo ""

echo "Step 2: Installing essential build tools..."
sudo apt install -y \
    build-essential \
    gcc \
    g++ \
    make \
    cmake \
    pkg-config
echo ""

echo "Step 3: Installing optional development tools..."
sudo apt install -y \
    valgrind \
    gdb \
    perf \
    strace \
    ltrace \
    || echo "Some optional tools failed to install (not critical)"
echo ""

echo "Step 4: Installing NUMA library (optional)..."
sudo apt install -y libnuma-dev || echo "NUMA library not installed (optional)"
echo ""

echo "Step 5: Verifying installed tools..."
echo ""

check_tool() {
    tool=$1
    if command -v $tool &> /dev/null; then
        version=$($tool --version 2>&1 | head -n1 || echo "installed")
        echo "  [OK] $tool: $version"
    else
        echo "  [MISSING] $tool"
    fi
}

echo "Essential tools:"
check_tool gcc
check_tool g++
check_tool make
check_tool cmake
echo ""

echo "Optional tools:"
check_tool valgrind
check_tool gdb
check_tool perf
echo ""

echo "Step 6: Setting up project directory..."
PROJECT_DIR="/mnt/e/001_code/005_memcode/nomalloc"

if [[ -d "$PROJECT_DIR" ]]; then
    echo "  Project directory found: $PROJECT_DIR"
    cd "$PROJECT_DIR"
    echo "  Current directory: $(pwd)"
else
    echo "  Project directory not found: $PROJECT_DIR"
    echo "  Please check the path or copy project to WSL"
fi
echo ""

echo "============================================"
echo "  Environment Setup Complete!"
echo "============================================"
echo ""

echo "To build and test nomalloc:"
echo ""
if [[ -d "$PROJECT_DIR" ]]; then
    echo "  cd $PROJECT_DIR"
    echo "  chmod +x quick_build.sh"
    echo "  ./quick_build.sh"
else
    echo "  1. Navigate to project directory"
    echo "  2. Run: chmod +x quick_build.sh"
    echo "  3. Run: ./quick_build.sh"
fi
echo ""

echo "WSL Tips:"
echo "  - Windows drives are mounted at /mnt/ (e.g., /mnt/c, /mnt/e)"
echo "  - Windows files are accessible from WSL"
echo "  - Use 'wsl' command from Windows PowerShell to enter WSL"
echo "  - Use 'exit' to exit WSL"
echo ""