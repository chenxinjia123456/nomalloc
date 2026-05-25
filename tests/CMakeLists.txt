cmake_minimum_required(VERSION 3.15)

add_subdirectory(unit)
add_subdirectory(benchmark)
add_subdirectory(stress)
add_subdirectory(sdv)

add_custom_target(run_all_tests
    DEPENDS run_unit_tests
    COMMENT "Running all tests"
)

add_custom_target(run_sdv_tests
    DEPENDS test_basic test_multithread test_stress
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/sdv/test_basic
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/sdv/test_multithread
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/sdv/test_stress
    COMMENT "Running all SDV tests"
)