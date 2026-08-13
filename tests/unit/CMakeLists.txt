include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(namfx_tests
    rt_alloc_test.cpp
    audio_graph_test.cpp
)

target_link_libraries(namfx_tests PRIVATE
    namfx_core
    namfx_warnings
    Catch2::Catch2WithMain
)

add_test(NAME namfx_tests COMMAND namfx_tests)
