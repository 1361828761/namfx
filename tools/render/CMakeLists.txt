add_executable(namfx_render
    namfx_render.cpp
)

target_link_libraries(namfx_render PRIVATE
    namfx_core
    namfx_warnings
    wavedigitalfilters
)

add_test(NAME namfx_render_demo
    COMMAND namfx_render --preset ${PROJECT_SOURCE_DIR}/core/preset/demo/warm.json
                         --seconds 0.2 --freq 220.0
                         --out ${CMAKE_BINARY_DIR}/render_demo_test.wav
)
