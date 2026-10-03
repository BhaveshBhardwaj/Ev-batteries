add_executable(ev_battery_bench
    math_utils_benchmark.cpp
    signal_processing_benchmark.cpp
)

target_link_libraries(ev_battery_bench
    PRIVATE
        ev_battery_core
        benchmark::benchmark_main
)

target_include_directories(ev_battery_bench
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(ev_battery_bench PRIVATE cxx_std_23)
