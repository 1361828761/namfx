# Desktop editor (M5a): JUCE GUI application. This is the only place JUCE
# may be used (engine red line 2: JUCE confined to desktop/).

if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../build/_deps/JUCE")
endif()

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(WARNING "JUCE not found at ${JUCE_DIR}; set -DJUCE_DIR=<path> to build the desktop editor")
    return()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE's juce_disable_default_flags() strips /FS; embedded debug info
# (Z7) avoids the shared-PDB clash when its big modules compile in parallel
if(MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
endif()

add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/_juce_build)

# JUCE's bundled third-party code (harfbuzz etc.) is not warning-clean;
# never let the propagated /WX fail its build
if(MSVC)
    foreach(juce_tgt juce_core juce_events juce_graphics juce_gui_basics juce_gui_extra
            juce_audio_basics juce_audio_devices juce_audio_formats juce_audio_utils)
        if(TARGET ${juce_tgt})
            target_compile_options(${juce_tgt} INTERFACE /WX- /W1)
        endif()
    endforeach()
endif()

juce_add_gui_app(namfx_editor
    PRODUCT_NAME "NAM Editor"
    VERSION "0.1.0"
    COMPANY_NAME "namfx"
    ICON_BIG ""
    ICON_SMALL ""
    NEEDS_WEB_BROWSER FALSE
    NEEDS_HELP FALSE
)

target_sources(namfx_editor PRIVATE
    App/nam_editor_app.cpp
    App/tuner_meter.cpp
    Engine/engine_audio_source.cpp
    Engine/engine_host.cpp
)

# juce_audio_utils pulls in the plugin client headers which define
# JucePlugin_Build_Standalone; use the custom-standalone START_JUCE_APPLICATION
# branch (a plain GUI app, not a plugin)
target_compile_definitions(namfx_editor PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

# ASIO is disabled in JUCE by default; enable it so ASIO4ALL / MOOER drivers
# show up as an "ASIO" device type (low-latency path for the GE100)
target_compile_definitions(namfx_editor PRIVATE JUCE_ASIO=1)

# the demo presets live in the source tree; embed the absolute path so the
# app finds them no matter what the working directory is (double-click start)
target_compile_definitions(namfx_editor PRIVATE
    "NAMFX_DEMO_DIR=\"${CMAKE_SOURCE_DIR}/core/preset/demo\""
)

target_include_directories(namfx_editor PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)

target_link_libraries(namfx_editor PRIVATE
    namfx_core
    nam_core
    namfx_warnings
    wavedigitalfilters
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_core
)

if(MSVC)
    # /Z7 (embedded debug info) bypasses the shared-PDB clash: JUCE's
    # juce_disable_default_flags() strips /FS and the CMake-level
    # DebugInformationFormat property, so force it on the command line;
    # /utf-8: the editor UI strings are Chinese (UTF-8), the system
    # codepage (GBK) would misread the bytes
    target_compile_options(namfx_editor PRIVATE /Z7 /W1 /utf-8)
endif()
