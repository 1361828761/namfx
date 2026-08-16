# NAMFX WebUI host (namfx_web): HTTP + SSE server and native audio backend
# over the shared engine. The static frontend lives in webui/www.

add_executable(namfx_web
    server/web_host.cpp
    # EngineHost is the desktop-shell control API over the shared engine;
    # the web host reuses it (its sources are core-only, no JUCE)
    ${CMAKE_SOURCE_DIR}/desktop/Engine/engine_host.cpp
    ${CMAKE_SOURCE_DIR}/desktop/Engine/engine_audio_source.cpp
    ${CMAKE_SOURCE_DIR}/desktop/Engine/native_audio_backend.cpp
)

target_include_directories(namfx_web PRIVATE ${CMAKE_SOURCE_DIR})

# the web root and demo presets live in the source tree; embed the absolute
# paths so the host finds them no matter what the working directory is
target_compile_definitions(namfx_web PRIVATE
    NAMFX_WEBUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/www"
    NAMFX_DEMO_DIR="${CMAKE_SOURCE_DIR}/core/preset/demo"
    NAMFX_NAM_DIR="${CMAKE_SOURCE_DIR}/nam"
    NAMFX_IR_DIR="${CMAKE_SOURCE_DIR}/ir"
    NAMFX_MODELS_DIR="${CMAKE_SOURCE_DIR}/modles"
    JUCE_ASIO=1
)

target_include_directories(namfx_web PRIVATE
    ${CMAKE_SOURCE_DIR}/desktop
)

target_link_libraries(namfx_web PRIVATE
    namfx_core
    nam_core
    namfx_warnings
    wavedigitalfilters
    nlohmann_json::nlohmann_json
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_core
)

if(MSVC)
    # /utf-8: host UI strings are Chinese (UTF-8); the system codepage (GBK)
    # would misread the bytes; getenv is fine for a shell process
    target_compile_options(namfx_web PRIVATE /utf-8)
    target_compile_definitions(namfx_web PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(namfx_web PRIVATE winmm ws2_32)
endif()
