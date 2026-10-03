add_library(ev_battery_gpr_completion
    gpr_completion.cpp
)

target_link_libraries(ev_battery_gpr_completion
    PUBLIC
        Eigen3::Eigen
)

target_include_directories(ev_battery_gpr_completion
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
