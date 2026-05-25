# WSL Installation Guide for Windows
# ===================================

This guide helps you set up a Linux environment on Windows using WSL2.

## Quick Installation (Recommended)

### Step 1: Install WSL

Open PowerShell as Administrator (Win + X -> Windows Terminal Admin), then run:

```powershell
wsl --install
```

This command will:
- Enable WSL feature
- Enable Virtual Machine Platform
- Download and install Ubuntu (default distribution)
- Install WSL kernel update package

### Step 2: Restart Computer

After installation completes, you MUST restart your computer.

### Step 3: Set Up Ubuntu User

After restart, Ubuntu will automatically launch. Create your user:
- Enter username (recommended: `user`)
- Enter password (twice)

## Alternative: Manual Installation

If `wsl --install` doesn't work, try manual steps:

### Enable WSL Feature

```powershell
# Run in Administrator PowerShell
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
```

### Enable Virtual Machine Platform

```powershell
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
```

### Restart Computer

```powershell
Restart-Computer
```

### Download and Install Linux Distribution

After restart, install Ubuntu:

```powershell
wsl --install -d Ubuntu
```

Or install from Microsoft Store:
- Open Microsoft Store
- Search "Ubuntu" or "Ubuntu 22.04 LTS"
- Click "Get" or "Install"

## Install Specific Distribution

Available distributions:
- Ubuntu (default)
- Ubuntu-22.04
- Debian
- Kali Linux
- OpenSUSE
- Alpine

Install command:
```powershell
wsl --install -d Ubuntu-22.04
```

## Post-Installation Setup

### Update System

Inside WSL (Ubuntu terminal):

```bash
sudo apt update
sudo apt upgrade -y
```

### Install Build Tools

```bash
sudo apt install -y build-essential cmake valgrind gdb perf
```

### Install NUMA Support (Optional)

```bash
sudo apt install -y libnuma-dev
```

## Verify Installation

### Check WSL Version

```powershell
wsl --version
```

### Check Installed Distributions

```powershell
wsl --list --verbose
```

### Test WSL

```bash
# Inside WSL
uname -a
gcc --version
cmake --version
```

## Common Issues

### Issue: WSL not found
Solution: Enable Windows features:
```powershell
Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Windows-Subsystem-Linux -NoRestart
Enable-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform -NoRestart
```

### Issue: Virtualization not enabled
Solution: Enable in BIOS/UEFI:
- Intel: Enable VT-x
- AMD: Enable AMD-V/SVM

### Issue: Kernel update required
Solution: Download and install:
https://wslstorestorage.blob.core.windows.net/wslblob/wsl_update_x64.msi

### Issue: WSL 1 to WSL 2 conversion
Solution:
```powershell
wsl --set-default-version 2
wsl --set-version Ubuntu 2
```

## WSL Commands

- Start WSL: `wsl`
- Run command: `wsl <command>`
- Run in specific distro: `wsl -d Ubuntu-22.04 <command>`
- Shutdown WSL: `wsl --shutdown`
- List distributions: `wsl --list --verbose`
- Set default distro: `wsl --set-default Ubuntu`

## Access Windows Files in WSL

Windows C drive is mounted at: `/mnt/c/`

```bash
# Access project directory
cd /mnt/c/Users/chenxinjia/Desktop/001_code/005_memcode/nomalloc
```

## Copy Files Between Windows and WSL

```bash
# Copy from Windows to WSL
cp /mnt/c/path/to/file ~/destination/

# Copy from WSL to Windows
cp ~/file /mnt/c/path/to/destination/
```

## Next Steps

After WSL is installed:

1. Navigate to project directory:
```bash
cd /mnt/e/001_code/005_memcode/nomalloc
```

2. Run quick build:
```bash
chmod +x quick_build.sh
./quick_build.sh
```

## References

- Official Guide: https://docs.microsoft.com/en-us/windows/wsl/install
- WSL GitHub: https://github.com/microsoft/WSL
- Ubuntu on WSL: https://ubuntu.com/wsl