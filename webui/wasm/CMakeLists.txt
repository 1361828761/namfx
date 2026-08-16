if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "webui/wasm requires the Emscripten toolchain")
endif()

set(NAMFX_WASM_DSP_SOURCES
    ${CMAKE_SOURCE_DIR}/core/audio/chain.cpp
    ${CMAKE_SOURCE_DIR}/core/audio/param_store.cpp
    ${CMAKE_SOURCE_DIR}/core/audio/output_stage.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/module_registry.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/gain.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/tone.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/ts808.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/klon.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/ocd.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/ota_comp.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/chorus.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/flanger.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/phaser.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/wah.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/ns2_gate.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/ge7_eq.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/dm2_delay.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/tape_delay.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/spring_reverb.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/pitch_shifter.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/octave.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/dsp/hall_reverb.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/ir/wav_io.cpp
    ${CMAKE_SOURCE_DIR}/core/modules/ir/cab_ir.cpp
)

# NAM Core is C++20 and carries its own Eigen/nlohmann; mirror the native
# nam_core target so the browser gets the same model DSP (whole-archive keeps
# the static architecture registrations alive)
if(NOT DEFINED NAM_CORE_COMMIT)
    set(NAM_CORE_COMMIT "daae2d0b482a5fe876258caf3fdfe177044b6295")
endif()
set(NAMFX_WASM_NAM_SOURCE_DIR
    "${CMAKE_SOURCE_DIR}/build/_deps/namcore-pinned/NeuralAmpModelerCore-${NAM_CORE_COMMIT}"
)
file(GLOB NAM_WASM_CORE_SOURCES
    "${NAMFX_WASM_NAM_SOURCE_DIR}/NAM/*.cpp"
    "${NAMFX_WASM_NAM_SOURCE_DIR}/NAM/*/*.cpp"
)
add_library(nam_wasm_core STATIC
    ${NAM_WASM_CORE_SOURCES}
    ${CMAKE_SOURCE_DIR}/core/modules/nam/nam_amp.cpp
)
target_include_directories(nam_wasm_core SYSTEM PUBLIC
    "${NAMFX_WASM_NAM_SOURCE_DIR}/NAM"
    "${NAMFX_WASM_NAM_SOURCE_DIR}/Dependencies/eigen"
    "${NAMFX_WASM_NAM_SOURCE_DIR}/Dependencies/nlohmann"
)
target_include_directories(nam_wasm_core PUBLIC
    ${CMAKE_SOURCE_DIR}/core
)
target_compile_features(nam_wasm_core PUBLIC cxx_std_20)
target_compile_definitions(nam_wasm_core PUBLIC NAM_SAMPLE_FLOAT NOMINMAX)
target_compile_options(nam_wasm_core PRIVATE -fexceptions)

add_executable(namfx_wasm
    namfx_wasm.cpp
    ${NAMFX_WASM_DSP_SOURCES}
)

target_include_directories(namfx_wasm PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/webui/wasm
)
target_compile_features(namfx_wasm PRIVATE cxx_std_17)
target_compile_definitions(namfx_wasm PRIVATE NAMFX_WASM_DSP_ONLY=1)
target_link_libraries(namfx_wasm PRIVATE
    wavedigitalfilters
    nlohmann_json::nlohmann_json
)

target_compile_options(namfx_wasm PRIVATE -fexceptions)
target_link_libraries(namfx_wasm PRIVATE
    nam_wasm_core
    wavedigitalfilters
    nlohmann_json::nlohmann_json
)
target_link_options(namfx_wasm PRIVATE
    -fexceptions
    "-Wl,--whole-archive"
    "$<TARGET_FILE:nam_wasm_core>"
    "-Wl,--no-whole-archive"
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sENVIRONMENT=web,worker"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sFILESYSTEM=0"
    "-sINCOMING_MODULE_JS_API=['wasmBinary','locateFile']"
    "--extern-pre-js=${CMAKE_CURRENT_SOURCE_DIR}/namfx_wasm_extern_pre.js"
    "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/namfx_wasm_pre.js"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_namfx_wasm_create','_namfx_wasm_destroy','_namfx_wasm_prepare','_namfx_wasm_load_preset_json','_namfx_wasm_register_asset','_namfx_wasm_set_param','_namfx_wasm_set_bypass','_namfx_wasm_set_mix','_namfx_wasm_set_output','_namfx_wasm_process','_namfx_wasm_get_levels','_namfx_wasm_state_json','_namfx_wasm_last_error']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8']"
)

set_target_properties(namfx_wasm PROPERTIES
    OUTPUT_NAME "namfx_wasm"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/webui/www/wasm"
)
