add_library(ev_battery_features
    features.cpp
)

target_link_libraries(ev_battery_features
    PUBLIC
        ev_battery_math_utils
        ev_battery_ica_dva
)

target_include_directories(ev_battery_features
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
