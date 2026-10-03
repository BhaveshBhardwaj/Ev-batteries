add_executable(ev_battery_tests
    common/test_types.cpp
    math_utils/test_calculus.cpp
    math_utils/test_filters.cpp
    math_utils/test_statistics.cpp
    synthetic/telemetry_generator.cpp
    synthetic/test_telemetry_generator.cpp
    ingestion/test_ingestion.cpp
    quality_score/test_quality_score.cpp
    cleaning/test_cleaning.cpp
    segmentation/test_segmentation.cpp
    denoising/test_denoising.cpp
    ica_dva/test_ica_dva.cpp
    features/test_features.cpp
    thermal/test_thermal.cpp
    calibration/test_calibration.cpp
    estimation/test_estimation.cpp
    uncertainty/test_uncertainty.cpp
    output/test_certificate.cpp
    gpr_completion/test_gpr_completion.cpp
    integration/test_pipeline_iteration_1.cpp
)

target_link_libraries(ev_battery_tests
    PRIVATE
        ev_battery_math_utils
        ev_battery_ingestion
        ev_battery_quality_score
        ev_battery_cleaning
        ev_battery_segmentation
        ev_battery_denoising
        ev_battery_gpr_completion
        ev_battery_ica_dva
        ev_battery_features
        ev_battery_thermal
        ev_battery_calibration
        ev_battery_estimation
        ev_battery_uncertainty
        ev_battery_output
        GTest::gtest_main
)

target_include_directories(ev_battery_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(ev_battery_tests PRIVATE cxx_std_23)

gtest_discover_tests(ev_battery_tests)
