#include <gtest/gtest.h>
#include <ev/battery/thermal/thermal.hpp>
#include <fstream>
#include <sstream>
#include <cmath>
#include <vector>

using namespace ev::battery;

namespace {

/**
 * @brief Physics-based lumped-parameter electro-thermal battery simulator.
 *
 * Implements the 1st-law energy balance equation:
 *   C_th * dT/dt = I(t)^2 * R_int(T) - h_conv * A * (T - T_amb)
 *
 * Models passive air cooling for Indian 2W/3W battery packs.
 */
class PhysicsThermalSimulator {
public:
    struct SimParams {
        double heat_capacity_j_k = 1800.0;    ///< Thermal mass C_th (J/K) for 16S pack module
        double internal_resistance_ohm = 0.025; ///< Base DCIR at 25°C
        double heat_transfer_coeff_w_k = 1.2; ///< Overall passive cooling dissipation (h*A in W/K)
        double ambient_temp_c = 45.0;         ///< Ambient air temperature (°C)
        double initial_temp_c = 45.0;         ///< Starting core temperature (°C)
    };

    explicit PhysicsThermalSimulator(SimParams params) : params_(params) {}

    common::InternalTimeSeries simulateDriveCycle(
        const std::vector<double>& current_profile_a,
        double dt_s = 1.0)
    {
        common::InternalTimeSeries ts;
        ts.session_id = "physics_thermal_sim";
        ts.chemistry = "LFP";
        ts.nominal_capacity_ah = 30.0;

        double t = 0.0;
        double current_temp_c = params_.initial_temp_c;

        ts.samples.reserve(current_profile_a.size());

        for (double i_a : current_profile_a) {
            ts.samples.push_back(common::Sample{
                .timestamp_s = t,
                .voltage_v = 51.2 - i_a * params_.internal_resistance_ohm,
                .current_a = i_a,
                .temperature_c = current_temp_c
            });

            // Physics ODE step: dT/dt = (Q_joule - Q_loss) / C_th
            double q_joule_w = i_a * i_a * params_.internal_resistance_ohm;
            double q_loss_w = params_.heat_transfer_coeff_w_k * (current_temp_c - params_.ambient_temp_c);
            double dt_dt = (q_joule_w - q_loss_w) / params_.heat_capacity_j_k;

            current_temp_c += dt_dt * dt_s;
            t += dt_s;
        }
        return ts;
    }

private:
    SimParams params_;
};

common::InternalTimeSeries loadNasaChargeCsv(const std::string& filepath) {
    common::InternalTimeSeries ts;
    ts.session_id = "NASA_B0005";
    ts.nominal_capacity_ah = 2.0;

    std::ifstream file(filepath);
    if (!file.is_open()) return ts;

    std::string line;
    if (!std::getline(file, line)) return ts;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string t_str, v_str, i_str, temp_str;
        if (std::getline(ss, t_str, ',') &&
            std::getline(ss, v_str, ',') &&
            std::getline(ss, i_str, ',') &&
            std::getline(ss, temp_str, ',')) {
            ts.samples.push_back(common::Sample{
                .timestamp_s = std::stod(t_str),
                .voltage_v = std::stod(v_str),
                .current_a = std::stod(i_str),
                .temperature_c = std::stod(temp_str)
            });
        }
    }
    return ts;
}

} // namespace

TEST(ThermalTest, ArrheniusFormulaThermodynamicExactness) {
    // At reference temp (25°C), multiplier must be exactly 1.0
    double k_25 = thermal::arrheniusAcceleration(25.0);
    EXPECT_NEAR(k_25, 1.0000, 1e-4);

    // At 45°C (Indian summer baseline), k(45) ~ 2.18 for 31.4 kJ/mol
    double k_45 = thermal::arrheniusAcceleration(45.0);
    EXPECT_NEAR(k_45, 2.183, 0.05);

    // At 55°C, aging acceleration is more than triple baseline
    double k_55 = thermal::arrheniusAcceleration(55.0);
    EXPECT_GT(k_55, 3.10);

    // At low temperatures (0°C), reaction rate is heavily depressed
    double k_0 = thermal::arrheniusAcceleration(0.0);
    EXPECT_LT(k_0, 0.40);
}

TEST(ThermalTest, PhysicsSimulationIndianSummerVsModerateClimate) {
    // Generate realistic 1-hour (3600 seconds) commercial delivery profile:
    // Repeated cycles of stop-and-go acceleration (35A), cruise (15A), regen (-10A), rest (0A)
    std::vector<double> profile;
    profile.reserve(3600);
    for (size_t i = 0; i < 3600; ++i) {
        size_t phase = i % 120;
        if (phase < 30) profile.push_back(35.0);      // High torque acceleration
        else if (phase < 80) profile.push_back(15.0); // Steady cruise
        else if (phase < 95) profile.push_back(-10.0);// Regenerative braking
        else profile.push_back(0.0);                  // Stop / delivery dropoff
    }

    // 1. Moderate climate (Western baseline: 22°C ambient)
    PhysicsThermalSimulator::SimParams mod_params;
    mod_params.ambient_temp_c = 22.0;
    mod_params.initial_temp_c = 22.0;
    PhysicsThermalSimulator mod_sim(mod_params);
    auto mod_ts = mod_sim.simulateDriveCycle(profile);

    thermal::ThermalConfig cfg;
    cfg.nominal_capacity_ah = 30.0;
    auto mod_report = thermal::analyzeThermalStress(mod_ts, cfg);
    ASSERT_TRUE(mod_report.has_value());

    EXPECT_LT(mod_report->thermal_degradation_index, 1.5);
    EXPECT_EQ(mod_report->severity, thermal::ThermalSeverity::NORMAL);
    EXPECT_EQ(mod_report->dwell_time_above_40c_s, 0.0);

    // 2. Indian Summer climate (Delhi/Ahmedabad fleet: 45°C ambient, passive cooling)
    PhysicsThermalSimulator::SimParams ind_params;
    ind_params.ambient_temp_c = 45.0;
    ind_params.initial_temp_c = 45.0;
    PhysicsThermalSimulator ind_sim(ind_params);
    auto ind_ts = ind_sim.simulateDriveCycle(profile);

    auto ind_report = thermal::analyzeThermalStress(ind_ts, cfg);
    ASSERT_TRUE(ind_report.has_value());

    // In Indian summer with passive cooling, temperature rises above ambient
    EXPECT_GE(ind_report->mean_temperature_c, 45.0);
    EXPECT_GT(ind_report->max_temperature_c, 48.0);
    EXPECT_GE(ind_report->thermal_degradation_index, 2.5); // Severe thermal penalty
    EXPECT_GE(ind_report->dwell_fraction_above_45c, 0.90); // Over 90% time above 45°C
    EXPECT_TRUE(ind_report->severity == thermal::ThermalSeverity::SEVERE ||
                ind_report->severity == thermal::ThermalSeverity::CRITICAL);
}

TEST(ThermalTest, PhysicsSimulationFastChargeHeatingRate) {
    // Simulate high-rate charging: 1.5C (45A) CC charge into passive-cooled pack for 20 minutes
    std::vector<double> charge_profile(1200, 45.0);

    PhysicsThermalSimulator::SimParams sim_params;
    sim_params.ambient_temp_c = 35.0;
    sim_params.initial_temp_c = 35.0;
    sim_params.heat_capacity_j_k = 1500.0; // Moderate pack thermal mass
    PhysicsThermalSimulator sim(sim_params);

    auto ts = sim.simulateDriveCycle(charge_profile);

    thermal::ThermalConfig cfg;
    cfg.nominal_capacity_ah = 30.0;
    cfg.dt_window_s = 60.0;

    auto report = thermal::analyzeThermalStress(ts, cfg);
    ASSERT_TRUE(report.has_value());

    // Temperature must increase significantly during 1.5C charging with passive cooling
    EXPECT_GT(report->max_temperature_c, report->min_temperature_c + 5.0);
    // Max heating rate should be physically positive and measurable
    EXPECT_GT(report->max_heating_rate_c_per_min, 0.50); // > 0.5 °C/min heating
}

TEST(ThermalTest, RealWorldNasaChamberBaseline) {
    std::string csv_path = "data/reference/nasa/B0005_charge_cycle_010.csv";
    auto ts = loadNasaChargeCsv(csv_path);
    if (ts.samples.empty()) {
        GTEST_SKIP() << "NASA CSV not found";
    }

    thermal::ThermalConfig cfg;
    cfg.nominal_capacity_ah = 2.0;

    auto report = thermal::analyzeThermalStress(ts, cfg);
    ASSERT_TRUE(report.has_value());

    // NASA Ames chamber was kept at 24°C nominal
    EXPECT_NEAR(report->mean_temperature_c, 24.5, 3.0);
    EXPECT_LT(report->thermal_degradation_index, 1.5);
    EXPECT_EQ(report->severity, thermal::ThermalSeverity::NORMAL);
}

TEST(ThermalTest, ErrorHandlingInsufficientSamples) {
    common::InternalTimeSeries empty_ts;
    auto res_empty = thermal::analyzeThermalStress(empty_ts);
    EXPECT_FALSE(res_empty.has_value());
    EXPECT_EQ(res_empty.error().code, "INSUFFICIENT_DATA");

    common::InternalTimeSeries single_ts;
    single_ts.samples.push_back({0.0, 52.0, 10.0, 25.0});
    auto res_single = thermal::analyzeThermalStress(single_ts);
    EXPECT_FALSE(res_single.has_value());
    EXPECT_EQ(res_single.error().code, "INSUFFICIENT_DATA");
}
