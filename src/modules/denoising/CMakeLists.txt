add_library(ev_battery_denoising STATIC
    denoising.cpp
)

target_include_directories(ev_battery_denoising
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ev_battery_denoising
    PUBLIC
        ev_battery_math_utils
)

target_compile_features(ev_battery_denoising PUBLIC cxx_std_23)
