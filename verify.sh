#!/bin/bash
cd /mnt/e/001_code/005_memcode/nomalloc || exit 1

echo '=== Nomalloc Verification ==='
echo ''
echo 'Files check:'
ls -la src/cache/lirs/ 2>/dev/null && echo '[OK] LIRS files' || echo '[MISSING] LIRS'
ls -la src/gc/ 2>/dev/null && echo '[OK] GC files' || echo '[MISSING] GC'
ls -la src/os/ 2>/dev/null && echo '[OK] OS files' || echo '[MISSING] OS'
ls -la src/debug/ 2>/dev/null && echo '[OK] Debug files' || echo '[MISSING] Debug'
echo ''

echo 'Tools check:'
which gcc >/dev/null 2>&1 && echo '[OK] gcc' || echo '[MISSING] gcc'
which cmake >/dev/null 2>&1 && echo '[OK] cmake' || echo '[MISSING] cmake'
echo ''

echo 'Build:'
rm -rf build
mkdir -p build
cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 | head -20 || true
cmake --build build -j4 2>&1 | tail -20 || true
echo ''

echo 'Artifacts:'
ls -lh build/*.a build/*.so 2>/dev/null || echo 'No artifacts'
