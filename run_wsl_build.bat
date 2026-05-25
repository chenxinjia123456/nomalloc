@echo off
REM Nomalloc WSL Build Verification
REM Run this script from Windows

echo ========================================
echo   Nomalloc WSL Build Verification
echo ========================================
echo.

REM Check WSL status
wsl -l -v
echo.

REM Navigate to project and run build
echo Starting WSL Ubuntu...
echo.

wsl -d Ubuntu -e bash -c "cd /mnt/e/001_code/005_memcode/nomalloc && echo '=== Checking Files ===' && ls src/cache/lirs/*.h src/cache/lirs/*.c && ls src/gc/*.h src/gc/*.c && ls src/os/*.h src/os/*.c && ls src/debug/*.h src/debug/*.c && echo '' && echo '=== Checking Build Tools ===' && which gcc && which cmake && echo '' && echo '=== Building Project ===' && rm -rf build && mkdir -p build && cmake -B build -DCMAKE_BUILD_TYPE=Release -DNOMALLOC_ENABLE_TESTS=ON -DNOMALLOC_ENABLE_NUMA=OFF -DNOMALLOC_ENABLE_HUGEPAGES=OFF 2>&1 | tail -20 && cmake --build build -j4 2>&1 | tail -30 && echo '' && echo '=== Checking Artifacts ===' && ls -lh build/*.a build/*.so 2>/dev/null && echo '' && echo '=== Running Example ===' && test -f build/examples/basic/hello_nomalloc && ./build/examples/basic/hello_nomalloc || echo 'Example not found' && echo '' && echo '=== Verification Complete ==='"

echo.
pause