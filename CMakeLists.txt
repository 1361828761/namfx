cmake_minimum_required(VERSION 3.24)
project(namfx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NAMFX_BUILD_TESTS "Build unit tests" ON)
option(NAMFX_BUILD_TOOLS "Build tools" ON)

enable_testing()

add_library(namfx_warnings INTERFACE)
if(MSVC)
    target_compile_options(namfx_warnings INTERFACE /W4 /WX)
else()
    target_compile_options(namfx_warnings INTERFACE -Wall -Wextra -Werror)
endif()

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    WaveDigitalFilters
    GIT_REPOSITORY https://github.com/jatinchowdhury18/WaveDigitalFilters.git
    GIT_TAG f391774967fa29aa595de6b594077167a9502765
    GIT_SHALLOW TRUE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_GetProperties(WaveDigitalFilters)
if(NOT WaveDigitalFilters_POPULATED)
    FetchContent_Populate(WaveDigitalFilters)
endif()
add_library(wavedigitalfilters INTERFACE)
target_include_directories(wavedigitalfilters INTERFACE ${wavedigitalfilters_SOURCE_DIR}/modules/chowdsp_wdf/include)

add_subdirectory(core)

if(NAMFX_BUILD_TESTS)
    add_subdirectory(tests/unit)
endif()

if(NAMFX_BUILD_TOOLS)
    add_subdirectory(tools/latency)
    add_subdirectory(tools/render)
endif()
