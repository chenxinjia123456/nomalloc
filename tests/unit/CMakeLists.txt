cmake_minimum_required(VERSION 3.15)

add_executable(unit_tests test_main.c)

target_link_libraries(unit_tests PRIVATE nomalloc pthread)

target_include_directories(unit_tests PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

set_target_properties(unit_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_test(NAME UnitTests COMMAND unit_tests)

add_custom_target(run_unit_tests
    COMMAND ${CMAKE_BINARY_DIR}/tests/unit_tests
    DEPENDS unit_tests
    COMMENT "Running unit tests"
)