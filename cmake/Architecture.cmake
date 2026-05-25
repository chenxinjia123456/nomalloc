include(CheckCCompilerFlag)

if(NOT DEFINED NOMALLOC_ARCH)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        set(NOMALLOC_ARCH "x86_64")
        set(NOMALLOC_ARCH_DEFINITIONS NOMALLOC_ARCH_X86_64=1)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
        set(NOMALLOC_ARCH "arm64")
        set(NOMALLOC_ARCH_DEFINITIONS NOMALLOC_ARCH_ARM64=1)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i686|i386|x86")
        set(NOMALLOC_ARCH "x86")
        set(NOMALLOC_ARCH_DEFINITIONS NOMALLOC_ARCH_X86=1)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        set(NOMALLOC_ARCH "arm")
        set(NOMALLOC_ARCH_DEFINITIONS NOMALLOC_ARCH_ARM=1)
    else()
        set(NOMALLOC_ARCH "generic")
        set(NOMALLOC_ARCH_DEFINITIONS NOMALLOC_ARCH_GENERIC=1)
    endif()
endif()

message(STATUS "Detected architecture: ${NOMALLOC_ARCH}")

# Skip architecture-specific flags on WSL/cross-filesystem builds
# They may cause compilation issues when building from Windows drives
if(NOT CMAKE_SOURCE_DIR MATCHES "/mnt/")
    if(NOMALLOC_ARCH STREQUAL "x86_64")
        check_c_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
        if(COMPILER_SUPPORTS_MARCH_NATIVE)
            list(APPEND NOMALLOC_COMPILE_OPTIONS "-march=native")
        endif()
    endif()
    
    if(NOMALLOC_ARCH STREQUAL "arm64")
        check_c_compiler_flag("-march=armv8-a" COMPILER_SUPPORTS_ARMV8_A)
        if(COMPILER_SUPPORTS_ARMV8_A)
            list(APPEND NOMALLOC_COMPILE_OPTIONS "-march=armv8-a")
        endif()
    endif()
endif()

set(NOMALLOC_CACHE_LINE_SIZE 64)

message(STATUS "Cache line size: ${NOMALLOC_CACHE_LINE_SIZE} bytes")

set(NOMALLOC_ARCH_DEFINITIONS ${NOMALLOC_ARCH_DEFINITIONS} CACHE_LINE_SIZE=${NOMALLOC_CACHE_LINE_SIZE})