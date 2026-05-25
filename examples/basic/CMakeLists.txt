cmake_minimum_required(VERSION 3.15)

add_executable(hello_nomalloc hello_nomalloc.c)

target_link_libraries(hello_nomalloc PRIVATE nomalloc pthread)

target_include_directories(hello_nomalloc PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(hello_nomalloc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples/basic
)