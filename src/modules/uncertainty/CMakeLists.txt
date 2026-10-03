add_library(ev_battery_uncertainty
    uncertainty.cpp
)

target_link_libraries(ev_battery_uncertainty
    PUBLIC
        ev_battery_estimation
)

target_include_directories(ev_battery_uncertainty
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
