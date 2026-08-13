add_executable(namfx_fuzz
    fuzz_main.cpp
)

target_link_libraries(namfx_fuzz PRIVATE
    namfx_core
    namfx_warnings
    wavedigitalfilters
)

add_test(NAME namfx_fuzz COMMAND namfx_fuzz --iters 300)
