add_executable(namfx_golden_tests
    golden_test.cpp
)

target_compile_definitions(namfx_golden_tests PRIVATE
    NAMFX_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

target_link_libraries(namfx_golden_tests PRIVATE
    namfx_core
    namfx_warnings
    wavedigitalfilters
    Catch2::Catch2WithMain
)

add_test(NAME namfx_golden_tests COMMAND namfx_golden_tests)

add_executable(namfx_golden_gen
    golden_gen.cpp
)

target_compile_definitions(namfx_golden_gen PRIVATE
    NAMFX_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

target_link_libraries(namfx_golden_gen PRIVATE
    namfx_core
    namfx_warnings
    wavedigitalfilters
)
