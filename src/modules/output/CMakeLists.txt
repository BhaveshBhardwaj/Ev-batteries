add_library(ev_battery_output
    sha256.cpp
    certificate.cpp
)

target_link_libraries(ev_battery_output
    PUBLIC
        ev_battery_quality_score
        ev_battery_cleaning
        ev_battery_segmentation
        ev_battery_denoising
        ev_battery_calibration
        ev_battery_thermal
        ev_battery_ica_dva
        ev_battery_features
        ev_battery_estimation
        ev_battery_uncertainty
)

target_include_directories(ev_battery_output
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
