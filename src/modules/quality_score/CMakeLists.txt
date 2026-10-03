add_library(ev_battery_quality_score
    quality_score.cpp
)

target_include_directories(ev_battery_quality_score
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
