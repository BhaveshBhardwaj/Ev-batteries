add_library(ev_battery_thermal
    thermal.cpp
)

target_link_libraries(ev_battery_thermal
    PUBLIC
        ev_battery_math_utils
)

target_include_directories(ev_battery_thermal
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
