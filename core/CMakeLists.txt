add_library(namfx_core STATIC
    platform/rt_alloc.cpp
    audio/audio_graph.cpp
    audio/chain.cpp
    audio/param_store.cpp
    modules/module_registry.cpp
    modules/dsp/gain.cpp
    modules/dsp/tone.cpp
    modules/dsp/ts808.cpp
    modules/dsp/klon.cpp
    modules/dsp/ocd.cpp
    preset/preset_model.cpp
    preset/preset_io.cpp
    preset/migration.cpp
    preset/atomic_write.cpp
    preset/backup.cpp
)

target_include_directories(namfx_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(namfx_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_definitions(namfx_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(namfx_core PUBLIC namfx_warnings wavedigitalfilters PRIVATE nlohmann_json::nlohmann_json)
