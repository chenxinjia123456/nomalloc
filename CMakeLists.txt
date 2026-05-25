cmake_minimum_required(VERSION 3.15)
project(nomalloc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(NOMALLOC_ENABLE_TESTS "Enable tests" ON)
option(NOMALLOC_ENABLE_DEBUG "Enable debug tools" ON)
option(NOMALLOC_ENABLE_NUMA "Enable NUMA support" ON)
option(NOMALLOC_ENABLE_HUGEPAGES "Enable huge pages support" ON)
option(NOMALLOC_ENABLE_SANITIZERS "Enable sanitizers for debug builds" OFF)

include(cmake/CompilerOptions.cmake)
include(cmake/Architecture.cmake)

set(NOMALLOC_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(NOMALLOC_SRC_DIR ${CMAKE_SOURCE_DIR}/src)

file(GLOB_RECURSE NOMALLOC_SOURCES
    ${NOMALLOC_SRC_DIR}/api/*.c
    ${NOMALLOC_SRC_DIR}/core/*.c
    ${NOMALLOC_SRC_DIR}/cache/*.c
    ${NOMALLOC_SRC_DIR}/memory/*.c
    ${NOMALLOC_SRC_DIR}/gc/*.c
    ${NOMALLOC_SRC_DIR}/os/*.c
    ${NOMALLOC_SRC_DIR}/utils/*.c
    ${NOMALLOC_SRC_DIR}/debug/*.c
    ${NOMALLOC_SRC_DIR}/config/*.c
)

file(GLOB NOMALLOC_HEADERS ${NOMALLOC_INCLUDE_DIR}/nomalloc/*.h)

add_library(nomalloc STATIC ${NOMALLOC_SOURCES})
target_include_directories(nomalloc PUBLIC ${NOMALLOC_INCLUDE_DIR})
target_include_directories(nomalloc PRIVATE ${NOMALLOC_SRC_DIR})

set_target_properties(nomalloc PROPERTIES
    OUTPUT_NAME nomalloc
    VERSION ${PROJECT_VERSION}
    POSITION_INDEPENDENT_CODE ON
)

target_compile_options(nomalloc PRIVATE ${NOMALLOC_COMPILE_OPTIONS})

# Use pthread directly instead of FindThreads for better cross-platform support
target_link_libraries(nomalloc PUBLIC pthread)

if(NOMALLOC_ENABLE_NUMA)
    include(cmake/FindNuma.cmake)
    if(NUMA_FOUND)
        target_compile_definitions(nomalloc PRIVATE NOMALLOC_NUMA_ENABLED=1)
        target_link_libraries(nomalloc PUBLIC numa)
    endif()
endif()

add_library(nomalloc_shared SHARED ${NOMALLOC_SOURCES})
target_include_directories(nomalloc_shared PUBLIC ${NOMALLOC_INCLUDE_DIR})
target_include_directories(nomalloc_shared PRIVATE ${NOMALLOC_SRC_DIR})
target_compile_options(nomalloc_shared PRIVATE ${NOMALLOC_COMPILE_OPTIONS})
target_link_libraries(nomalloc_shared PUBLIC pthread)
set_target_properties(nomalloc_shared PROPERTIES
    OUTPUT_NAME nomalloc
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    POSITION_INDEPENDENT_CODE ON
)

if(NOMALLOC_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NOMALLOC_ENABLE_DEBUG)
    target_compile_definitions(nomalloc PRIVATE NOMALLOC_DEBUG_ENABLED=1)
endif()

install(TARGETS nomalloc nomalloc_shared
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${NOMALLOC_HEADERS} DESTINATION include/nomalloc)

install(FILES ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/LICENSE
    ${CMAKE_SOURCE_DIR}/VERSION
    DESTINATION share/doc/nomalloc
)

add_custom_target(clean-all
    COMMAND ${CMAKE_BUILD_TOOL} clean
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/cmake_clean.cmake
    COMMENT "Cleaning all build artifacts"
)

message(STATUS "Nomalloc ${PROJECT_VERSION}")
message(STATUS "  Architecture: ${NOMALLOC_ARCH}")
message(STATUS "  Tests: ${NOMALLOC_ENABLE_TESTS}")
message(STATUS "  Debug tools: ${NOMALLOC_ENABLE_DEBUG}")
message(STATUS "  NUMA: ${NOMALLOC_ENABLE_NUMA}")
message(STATUS "  Huge pages: ${NOMALLOC_ENABLE_HUGEPAGES}")