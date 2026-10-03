add_library(ev_battery_cleaning STATIC
    cleaning.cpp
)

target_include_directories(ev_battery_cleaning
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ev_battery_cleaning
    PUBLIC
        ev_battery_math_utils
)

target_compile_features(ev_battery_cleaning PUBLIC cxx_std_23)
