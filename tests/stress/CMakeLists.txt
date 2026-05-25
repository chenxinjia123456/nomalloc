cmake_minimum_required(VERSION 3.15)

add_executable(stress_long_running long_running.c)

target_link_libraries(stress_long_running PRIVATE nomalloc pthread)

target_include_directories(stress_long_running PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(stress_long_running PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/stress
)