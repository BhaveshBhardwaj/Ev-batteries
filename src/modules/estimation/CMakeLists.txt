add_library(ev_battery_estimation
    estimation.cpp
)

target_link_libraries(ev_battery_estimation
    PUBLIC
        ev_battery_features
        ev_battery_thermal
        ev_battery_calibration
)

target_include_directories(ev_battery_estimation
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
