include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.0
    GIT_SHALLOW TRUE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(namfx_tests
    rt_alloc_test.cpp
    audio_graph_test.cpp
    registry_test.cpp
    param_spec_test.cpp
    spsc_queue_test.cpp
    param_store_test.cpp
    rt_safety_test.cpp
    chain_test.cpp
    crossfade_test.cpp
    graph_exchange_test.cpp
    preset_io_test.cpp
    migration_test.cpp
    integration_test.cpp
    ts808_test.cpp
    klon_test.cpp
    ocd_test.cpp
    ota_comp_test.cpp
)

file(COPY ${PROJECT_SOURCE_DIR}/core/preset/demo/ DESTINATION ${CMAKE_BINARY_DIR}/demo-presets)

file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/demo-presets" NAMFX_DEMO_DIR_CMAKE)
target_compile_definitions(namfx_tests PRIVATE NAMFX_DEMO_DIR="${NAMFX_DEMO_DIR_CMAKE}")

target_link_libraries(namfx_tests PRIVATE
    namfx_core
    namfx_warnings
    Catch2::Catch2WithMain
)

add_test(NAME namfx_tests COMMAND namfx_tests)
