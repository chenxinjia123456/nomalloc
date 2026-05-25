@echo off
chcp 65001 >nul
echo.
echo ============================================
echo   WSL Installation Commands
echo ============================================
echo.
echo 请在管理员 PowerShell 中运行以下命令:
echo.
echo 步骤 1: 启用 WSL 功能
echo   dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
echo.
echo 步骤 2: 启用虚拟机平台
echo   dism.exe /online /enable-feature /featurename=VirtualMachinePlatform /all /norestart
echo.
echo 步骤 3: 重启计算机
echo   Restart-Computer
echo.
echo 步骤 4: 重启后安装 Ubuntu
echo   wsl --install -d Ubuntu
echo.
echo ============================================
echo.
echo 注意: 命令是 "wsl" 不是 "wls"
echo ============================================
pause