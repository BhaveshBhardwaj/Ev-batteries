add_library(ev_battery_segmentation STATIC
    segmentation.cpp
)

target_include_directories(ev_battery_segmentation
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ev_battery_segmentation
    PUBLIC
        ev_battery_math_utils
)

target_compile_features(ev_battery_segmentation PUBLIC cxx_std_23)
