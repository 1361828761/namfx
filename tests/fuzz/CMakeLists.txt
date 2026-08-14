add_executable(namfx_fuzz
    fuzz_main.cpp
)

target_link_libraries(namfx_fuzz PRIVATE
    namfx_core
    nam_core
    namfx_warnings
    wavedigitalfilters
)

file(COPY ${PROJECT_SOURCE_DIR}/tests/unit/assets/ DESTINATION ${CMAKE_BINARY_DIR}/test-assets)
file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/test-assets" NAMFX_TEST_ASSETS_DIR_CMAKE)
target_compile_definitions(namfx_fuzz PRIVATE NAMFX_TEST_ASSETS_DIR="${NAMFX_TEST_ASSETS_DIR_CMAKE}")

add_test(NAME namfx_fuzz COMMAND namfx_fuzz --iters 300)
