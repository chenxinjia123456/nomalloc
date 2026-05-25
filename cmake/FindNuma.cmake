# Try to find NUMA library
#
# NUMA_FOUND - System has NUMA
# NUMA_INCLUDE_DIRS - The NUMA include directories
# NUMA_LIBRARIES - The NUMA libraries

find_path(NUMA_INCLUDE_DIR NAMES numa.h
    PATHS
    /usr/include
    /usr/local/include
    /opt/include
)

find_library(NUMA_LIBRARY NAMES numa
    PATHS
    /usr/lib
    /usr/local/lib
    /opt/lib
    /usr/lib/x86_64-linux-gnu
    /usr/lib/aarch64-linux-gnu
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NUMA DEFAULT_MSG NUMA_LIBRARY NUMA_INCLUDE_DIR)

mark_as_advanced(NUMA_INCLUDE_DIR NUMA_LIBRARY)

set(NUMA_LIBRARIES ${NUMA_LIBRARY})
set(NUMA_INCLUDE_DIRS ${NUMA_INCLUDE_DIR})