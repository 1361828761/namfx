add_executable(engine_latency
    engine_latency.cpp
)

target_link_libraries(engine_latency PRIVATE namfx_core namfx_warnings)
