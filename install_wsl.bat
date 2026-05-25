@echo off
REM WSL Installation Helper Script for Windows
REM ===========================================
REM This script helps install WSL2 on Windows

echo ============================================
echo   WSL Installation Helper Script
echo ============================================
echo.

REM Check if running as administrator
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script requires Administrator privileges.
    echo.
    echo Please run this script as Administrator:
    echo   1. Right-click this script
    echo   2. Select "Run as administrator"
    echo.
    pause
    exit /b 1
)

echo This script will help you install WSL2 on Windows.
echo.
echo Prerequisites:
echo   - Windows 10 version 2004+ or Windows 11
echo   - Virtualization enabled in BIOS (VT-x/AMD-V)
echo   - Internet connection
echo.
pause

echo.
echo Step 1: Checking Windows version...
ver
echo.

echo Step 2: Enabling WSL feature...
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
if %errorLevel% neq 0 (
    echo Warning: WSL feature enablement may have failed.
)

echo.
echo Step 3: Enabling Virtual Machine Platform...
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
if %errorLevel% neq 0 (
    echo Warning: VM Platform enablement may have failed.
)

echo.
echo Step 4: Setting WSL default version to 2...
wsl --set-default-version 2

echo.
echo ============================================
echo   Installation Steps Completed
echo ============================================
echo.
echo Next steps:
echo.
echo 1. RESTART your computer (REQUIRED)
echo    - Save all work
echo    - Restart Windows
echo.
echo 2. After restart, open PowerShell and run:
echo    wsl --install -d Ubuntu
echo.
echo 3. Create Ubuntu user when prompted:
echo    - Enter username (e.g., user)
echo    - Enter password twice
echo.
echo 4. Update Ubuntu and install build tools:
echo    sudo apt update
echo    sudo apt upgrade -y
echo    sudo apt install -y build-essential cmake
echo.
echo 5. Navigate to project and run build:
echo    cd /mnt/e/001_code/005_memcode/nomalloc
echo    chmod +x quick_build.sh
echo    ./quick_build.sh
echo.
echo ============================================
pause