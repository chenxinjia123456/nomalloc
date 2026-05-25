cmake_minimum_required(VERSION 3.15)

option(ENABLE_JEMALLOC_BENCHMARK "Enable jemalloc benchmark comparison" ON)

if(ENABLE_JEMALLOC_BENCHMARK)
    find_package(jemalloc QUIET)
    
    if(jemalloc_FOUND)
        message(STATUS "jemalloc found: ${jemalloc_INCLUDE_DIRS}")
    else()
        message(STATUS "jemalloc not found, will use system malloc for comparison")
    endif()
endif()

add_executable(single_thread_bench single_thread.c)

target_link_libraries(single_thread_bench PRIVATE nomalloc pthread)

target_include_directories(single_thread_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(single_thread_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/benchmark
)

add_executable(multi_thread_bench multi_thread.c)

target_link_libraries(multi_thread_bench PRIVATE nomalloc pthread)

target_include_directories(multi_thread_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(multi_thread_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/benchmark
)

if(ENABLE_JEMALLOC_BENCHMARK)
    add_executable(comparative_bench comparative_bench.c)
    
    target_link_libraries(comparative_bench PRIVATE nomalloc pthread)
    
    if(jemalloc_FOUND)
        target_link_libraries(comparative_bench PRIVATE jemalloc::jemalloc)
        target_compile_definitions(comparative_bench PRIVATE JEMALLOC_ENABLED=1)
    endif()
    
    target_include_directories(comparative_bench PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party
    )
    
    set_target_properties(comparative_bench PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/benchmark
    )
endif()

add_executable(latency_bench latency_bench.c)

target_link_libraries(latency_bench PRIVATE nomalloc pthread)

target_include_directories(latency_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(latency_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/benchmark
)

add_executable(fragmentation_bench fragmentation_bench.c)

target_link_libraries(fragmentation_bench PRIVATE nomalloc pthread)

target_include_directories(fragmentation_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(fragmentation_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/benchmark
)

add_executable(gc_bench gc_bench.c)

target_link_libraries(gc_bench PRIVATE nomalloc pthread)

target_include_directories(gc_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(gc_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/benchmark
)

add_executable(numa_bench numa_bench.c)

target_link_libraries(numa_bench PRIVATE nomalloc pthread)

target_include_directories(numa_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

if(NOMALLOC_ENABLE_NUMA AND NUMA_FOUND)
    target_link_libraries(numa_bench PRIVATE numa)
endif()

set_target_properties(numa_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/benchmark
)

add_custom_target(run_benchmarks
    COMMAND ${CMAKE_BINARY_DIR}/tests/benchmark/single_thread_bench
    COMMAND ${CMAKE_BINARY_DIR}/tests/benchmark/multi_thread_bench
    COMMAND ${CMAKE_BINARY_DIR}/tests/benchmark/latency_bench
    COMMAND ${CMAKE_BINARY_DIR}/tests/benchmark/fragmentation_bench
    DEPENDS single_thread_bench multi_thread_bench latency_bench fragmentation_bench
    COMMENT "Running all benchmarks"
)

if(ENABLE_JEMALLOC_BENCHMARK AND jemalloc_FOUND)
    add_custom_target(run_comparative_bench
        COMMAND ${CMAKE_BINARY_DIR}/tests/benchmark/comparative_bench
        DEPENDS comparative_bench
        COMMENT "Running comparative benchmark against jemalloc"
    )
endif()