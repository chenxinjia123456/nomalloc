cmake_minimum_required(VERSION 3.10)
project(nomalloc_sdv_tests)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(test_basic test_basic.c)
add_executable(test_multithread test_multithread.c)
add_executable(test_stress test_stress.c)
add_executable(test_simple test_simple.c)
add_executable(test_aligned_simple test_aligned_simple.c)
add_executable(test_minimal test_minimal.c)

foreach(test_name test_basic test_multithread test_stress test_simple test_aligned_simple test_minimal)
    target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/../../include)
    target_link_libraries(${test_name} PRIVATE nomalloc pthread)
endforeach()