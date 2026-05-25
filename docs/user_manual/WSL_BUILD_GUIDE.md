# Nomalloc - Windows WSL Build Guide

## Overview

This guide explains how to build and test nomalloc on Windows using WSL (Windows Subsystem for Linux).

## Prerequisites

- Windows 10 (version 2004+) or Windows 11
- Administrator access
- Virtualization enabled in BIOS (VT-x for Intel, AMD-V/SVM for AMD)
- Internet connection
- ~10 GB disk space

## Step 1: Install WSL2

### Quick Install (Recommended)

1. Open PowerShell as Administrator:
   - Press `Win + X`
   - Select "Windows Terminal (Administrator)" or "PowerShell (Administrator)"

2. Run the install command:
   ```powershell
   wsl --install
   ```

3. Restart your computer

### Manual Install (If Quick Install Fails)

1. Enable WSL feature:
   ```powershell
   dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
   ```

2. Enable Virtual Machine Platform:
   ```powershell
   dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
   ```

3. Restart computer:
   ```powershell
   Restart-Computer
   ```

4. After restart, install Ubuntu:
   ```powershell
   wsl --install -d Ubuntu
   ```

### Set Up Ubuntu User

After installation, Ubuntu will launch automatically:
1. Enter username (e.g., `user`)
2. Enter password (twice)

## Step 2: Set Up Development Environment

### Option A: Use Setup Script (Recommended)

After WSL is installed, copy project to accessible location and run:

```bash
# Inside WSL terminal
cd /mnt/e/001_code/005_memcode/nomalloc
chmod +x setup_wsl_env.sh
./setup_wsl_env.sh
```

### Option B: Manual Setup

```bash
# Update system
sudo apt update
sudo apt upgrade -y

# Install build tools
sudo apt install -y build-essential cmake

# Install optional tools
sudo apt install -y valgrind gdb perf

# Install NUMA library (optional)
sudo apt install -y libnuma-dev
```

## Step 3: Build and Test

### Using Build Script

```bash
# Inside WSL, navigate to project
cd /mnt/e/001_code/005_memcode/nomalloc

# Run build script (designed for WSL)
chmod +x build_in_wsl.sh
./build_in_wsl.sh
```

### Manual Build

```bash
# Navigate to project
cd /mnt/e/001_code/005_memcode/nomalloc

# Create build directory
mkdir -p build
cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release -DNOMALLOC_ENABLE_TESTS=ON

# Build
cmake --build . -j$(nproc)

# Test
./tests/unit/unit_tests
./tests/benchmark/single_thread_bench
```

## WSL Tips

### Access Windows Files
Windows drives are mounted in `/mnt/`:
- C: drive -> `/mnt/c/`
- E: drive -> `/mnt/e/`

Example:
```bash
cd /mnt/e/001_code/005_memcode/nomalloc
```

### Copy Files
```bash
# From Windows to WSL
cp /mnt/c/Users/yourname/file.txt ~/documents/

# From WSL to Windows
cp ~/result.txt /mnt/c/Users/yourname/Desktop/
```

### Run Windows Programs
```bash
# Run Windows executable from WSL
/mnt/c/Windows/System32/cmd.exe /C echo "Hello"
```

### Exit WSL
```bash
exit
# or press Ctrl+D
```

### Restart WSL
```powershell
# From Windows PowerShell
wsl --shutdown
wsl
```

## Performance Considerations

### WSL Performance Tips

1. **Store project in WSL filesystem (faster)**
   ```bash
   mkdir ~/projects
   cp -r /mnt/e/001_code/005_memcode/nomalloc ~/projects/
   cd ~/projects/nomalloc
   ```
   WSL filesystem is faster than accessing Windows drives.

2. **Disable NUMA in build**
   WSL doesn't support NUMA, disable it:
   ```bash
   cmake -DNOMALLOC_ENABLE_NUMA=OFF ..
   ```

3. **Disable huge pages**
   WSL doesn't support huge pages, disable:
   ```bash
   cmake -DNOMALLOC_ENABLE_HUGEPAGES=OFF ..
   ```

### Full Configuration for WSL

```bash
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DNOMALLOC_ENABLE_TESTS=ON \
    -DNOMALLOC_ENABLE_NUMA=OFF \
    -DNOMALLOC_ENABLE_HUGEPAGES=OFF
```

## Troubleshooting

### Issue: WSL command not found
Solution: Install WSL:
```powershell
wsl --install
```

### Issue: Virtualization error
Solution: Enable in BIOS:
- Intel: Enable VT-x in BIOS
- AMD: Enable AMD-V/SVM in BIOS

### Issue: cmake fails with NUMA error
Solution: Disable NUMA:
```bash
cmake -DNOMALLOC_ENABLE_NUMA=OFF ..
```

### Issue: mmap fails with huge pages
Solution: Disable huge pages:
```bash
cmake -DNOMALLOC_ENABLE_HUGEPAGES=OFF ..
```

### Issue: Permission denied
Solution: Check file permissions:
```bash
chmod +x build_in_wsl.sh
ls -la
```

### Issue: Slow build on Windows drive
Solution: Copy to WSL filesystem:
```bash
cp -r /mnt/e/001_code/005_memcode ~/projects/
cd ~/projects/005_memcode/nomalloc
```

## Test Results

After successful build, you should see:

```
=== Nomalloc Unit Tests ===
Tests run:    15
Tests passed: 15
Tests failed: 0
All tests PASSED!

=== Nomalloc Single-Thread Benchmark ===
Throughput: 40 M ops/sec
Average latency: 25 ns/op
```

## Next Steps

After successful build:
1. Review test output
2. Check performance benchmarks
3. Compare with jemalloc (if available)
4. Continue implementing remaining features:
   - LIRS cache eviction
   - Garbage collection
   - Debugging tools

## Resources

- [WSL Official Documentation](https://docs.microsoft.com/en-us/windows/wsl/)
- [WSL Installation Guide](https://docs.microsoft.com/en-us/windows/wsl/install)
- [WSL GitHub](https://github.com/microsoft/WSL)
- [Ubuntu on WSL](https://ubuntu.com/wsl)