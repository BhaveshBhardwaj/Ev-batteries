add_library(ev_battery_ica_dva
    ica_dva.cpp
)

target_link_libraries(ev_battery_ica_dva
    PUBLIC
        ev_battery_math_utils
        ev_battery_segmentation
        ev_battery_denoising
)

target_include_directories(ev_battery_ica_dva
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
