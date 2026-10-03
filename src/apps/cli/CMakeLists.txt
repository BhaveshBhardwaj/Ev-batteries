add_executable(ev_battery_cli
    main.cpp
)

target_link_libraries(ev_battery_cli
    PRIVATE
        ev_battery_core
)

target_include_directories(ev_battery_cli
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(ev_battery_cli PRIVATE cxx_std_23)
