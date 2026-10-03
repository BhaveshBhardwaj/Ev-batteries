add_library(ev_battery_calibration
    calibration.cpp
)

target_link_libraries(ev_battery_calibration
    PUBLIC
        ev_battery_math_utils
        ev_battery_segmentation
)

target_include_directories(ev_battery_calibration
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
