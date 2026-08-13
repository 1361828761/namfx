add_library(namfx_core STATIC
    platform/rt_alloc.cpp
    audio/audio_graph.cpp
)

target_include_directories(namfx_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(namfx_core PUBLIC cxx_std_17)

target_link_libraries(namfx_core PUBLIC namfx_warnings)
