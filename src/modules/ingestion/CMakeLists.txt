add_library(ev_battery_ingestion
    ingestion.cpp
)

target_include_directories(ev_battery_ingestion
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
