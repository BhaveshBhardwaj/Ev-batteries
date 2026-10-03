add_library(ev_battery_math_utils STATIC
    calculus.cpp
    filters.cpp
    statistics.cpp
)

target_include_directories(ev_battery_math_utils
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ev_battery_math_utils
    PUBLIC
        Eigen3::Eigen
)

target_compile_features(ev_battery_math_utils PUBLIC cxx_std_23)
