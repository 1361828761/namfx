add_library(namfx_core STATIC
    platform/rt_alloc.cpp
    audio/audio_graph.cpp
    audio/chain.cpp
    audio/param_store.cpp
    audio/scene_engine.cpp
    audio/control_router.cpp
    midi/midi_router.cpp
    modules/module_registry.cpp
    modules/dsp/gain.cpp
    modules/dsp/tone.cpp
    modules/dsp/ts808.cpp
    modules/dsp/klon.cpp
    modules/dsp/ocd.cpp
    modules/dsp/ota_comp.cpp
    modules/dsp/chorus.cpp
    modules/dsp/flanger.cpp
    modules/dsp/phaser.cpp
    modules/dsp/wah.cpp
    modules/dsp/ns2_gate.cpp
    modules/dsp/ge7_eq.cpp
    modules/dsp/dm2_delay.cpp
    modules/dsp/tape_delay.cpp
    modules/dsp/spring_reverb.cpp
    modules/dsp/pitch_shifter.cpp
    modules/dsp/octave.cpp
    modules/dsp/hall_reverb.cpp
    modules/ir/wav_io.cpp
    modules/ir/cab_ir.cpp
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

target_link_libraries(namfx_core PUBLIC namfx_warnings PRIVATE wavedigitalfilters nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------------------
# NAM Core: official NeuralAmpModelerCore pinned at a fixed commit, wrapped
# as an isolated static library (PLAN §6: SIMD/C++20 confined to this
# target). The module glue (nam_amp.cpp) lives here too so no C++20/NAM
# headers leak into the C++17 core. Consumers must link nam_core AND call
# registerNamAmp() (the registration symbol lives in this library, which
# links against namfx_core — the reverse direction would be circular).
# ---------------------------------------------------------------------------
if(NOT DEFINED NAM_CORE_COMMIT)
    set(NAM_CORE_COMMIT "daae2d0b482a5fe876258caf3fdfe177044b6295")
endif()

include(FetchContent)
FetchContent_Declare(
    nam_core
    URL https://codeload.github.com/sdatkinson/NeuralAmpModelerCore/tar.gz/${NAM_CORE_COMMIT}
    URL_HASH SHA256=1f850e09a2b96cf07825ee569c3015add704a095bcc2409ff3df2ed6922fd509
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    UPDATE_DISCONNECTED TRUE
)
set(FETCHCONTENT_SOURCE_DIR_NAM_CORE
    "${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/namcore-pinned/NeuralAmpModelerCore-${NAM_CORE_COMMIT}"
    CACHE STRING "Local NAM Core checkout override (proxy-friendly)")
# populate only (no add_subdirectory: the upstream build pulls tools/ whose
# AudioDSPTools submodule is absent from tarballs)
FetchContent_GetProperties(nam_core)
if(NOT nam_core_POPULATED)
    FetchContent_Populate(nam_core)
endif()

file(GLOB NAM_CORE_SOURCES
    "${nam_core_SOURCE_DIR}/NAM/*.cpp"
    "${nam_core_SOURCE_DIR}/NAM/*/*.cpp"
)
add_library(nam_core STATIC
    ${NAM_CORE_SOURCES}
    modules/nam/nam_amp.cpp
)
target_include_directories(nam_core SYSTEM PUBLIC
    "${nam_core_SOURCE_DIR}/NAM"
    "${nam_core_SOURCE_DIR}/Dependencies/eigen"
    "${nam_core_SOURCE_DIR}/Dependencies/nlohmann"
)
target_include_directories(nam_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(nam_core PUBLIC cxx_std_20)
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT NOMINMAX)
# deliberately NOT linked to namfx_core/namfx_warnings: as a static library
# it needs no link-time declaration, and the /WX propagation would fail the
# upstream NAM sources; consumers link namfx_core + nam_core explicitly
if(MSVC)
    target_compile_definitions(nam_core PRIVATE _CRT_SECURE_NO_WARNINGS)
    # architecture parsers register themselves via static objects; keep every
    # object file when linking the static library
    target_link_options(nam_core INTERFACE "/WHOLEARCHIVE:nam_core")
endif()
