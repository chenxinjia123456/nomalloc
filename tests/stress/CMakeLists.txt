cmake_minimum_required(VERSION 3.15)

add_executable(stress_long_running long_running.c)
target_link_libraries(stress_long_running PRIVATE nomalloc pthread)
target_include_directories(stress_long_running PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
set_target_properties(stress_long_running PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/stress
)

add_executable(stress_reliability reliability_test.c)
target_link_libraries(stress_reliability PRIVATE nomalloc pthread)
target_include_directories(stress_reliability PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
set_target_properties(stress_reliability PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/stress
)

add_executable(stress_pressure pressure_test.c)
target_link_libraries(stress_pressure PRIVATE nomalloc pthread)
target_include_directories(stress_pressure PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
set_target_properties(stress_pressure PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/stress
)

add_executable(stress_concurrent concurrent_stress_test.c)
target_link_libraries(stress_concurrent PRIVATE nomalloc pthread)
target_include_directories(stress_concurrent PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
set_target_properties(stress_concurrent PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/stress
)

add_executable(stress_consistency memory_consistency_test.c)
target_link_libraries(stress_consistency PRIVATE nomalloc pthread)
target_include_directories(stress_consistency PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
set_target_properties(stress_consistency PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/stress
)

add_executable(stress_benchmark benchmark_stress_test.c)
target_link_libraries(stress_benchmark PRIVATE nomalloc pthread)
target_include_directories(stress_benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
set_target_properties(stress_benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/stress
)