#include "telemetry_generator.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace ev::battery::testing {

TelemetryGenerator::TelemetryGenerator(GeneratorConfig config)
    : config_(std::move(config)) {}

double TelemetryGenerator::cellOcv(double soc) const {
    // Clamp soc to [0.001, 0.999] for numerical safety
    const double z = std::clamp(soc, 0.001, 0.999);

    if (config_.chemistry == "LFP") {
        // Physical LFP OCV model with 3 phase transitions (Safari & Delacourt, 2011)
        // Characterized by very flat 3.3V plateau and prominent phase transitions
        const double base = 3.25;
        const double slope = 0.12 * z;
        const double lower_knee = -0.08 * std::exp(-35.0 * z);
        const double upper_knee = 0.28 * std::exp(45.0 * (z - 1.0));
        
        // Electrochemical phase transitions (forming dQ/dV peaks)
        const double phase1 = 0.035 * std::tanh(25.0 * (z - 0.22));
        const double phase2 = 0.025 * std::tanh(30.0 * (z - 0.55));
        const double phase3 = 0.020 * std::tanh(35.0 * (z - 0.78));

        return base + slope + lower_knee + upper_knee + phase1 + phase2 + phase3;
    } else {
        // Standard NMC cell OCV (sloped 3.0V to 4.2V curve)
        return 3.0 + 1.15 * z + 0.05 * std::log(z + 0.01) - 0.05 * std::exp(25.0 * (z - 1.0));
    }
}

GroundTruthSession TelemetryGenerator::generateChargeSegment(
    double charge_current_a,
    double start_soc,
    double end_soc,
    bool add_noise,
    uint32_t seed
) {
    GroundTruthSession session;
    session.telemetry.session_id = "synth_charge_001";
    session.telemetry.pack_id = "pack_LFP_48V_30Ah";
    session.telemetry.chemistry = config_.chemistry;
    session.telemetry.nominal_capacity_ah = config_.nominal_capacity_ah;

    std::mt19937 rng(seed);
    std::normal_distribution<double> v_noise(0.0, config_.voltage_noise_std_v);
    std::normal_distribution<double> i_noise(0.0, config_.current_noise_std_a);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    std::uniform_real_distribution<double> spike_sign(-1.0, 1.0);

    const double dt = config_.sampling_interval_s;
    const double q_total_as = config_.nominal_capacity_ah * 3600.0;
    const double r0_pack = 0.0018 * static_cast<double>(config_.series_cells); // ~28 mOhm
    const double r1_pack = 0.0012 * static_cast<double>(config_.series_cells); // ~19 mOhm
    const double tau_rc = 25.0; // 25s polarization time constant

    double soc = start_soc;
    double t = 0.0;
    double v_rc = 0.0;
    double temp_c = config_.ambient_temp_c;

    while (soc < end_soc) {
        // State update
        const double i_true = charge_current_a;
        soc += (i_true * dt) / q_total_as;
        v_rc = v_rc * std::exp(-dt / tau_rc) + (i_true * r1_pack) * (1.0 - std::exp(-dt / tau_rc));

        const double ocv_pack = cellOcv(soc) * static_cast<double>(config_.series_cells);
        const double v_true = ocv_pack + i_true * r0_pack + v_rc;

        // Thermal Joule heating
        const double power_loss = i_true * i_true * (r0_pack + r1_pack);
        temp_c += dt * (power_loss * 0.001 - (temp_c - config_.ambient_temp_c) * 0.005);

        // Record Ground Truth
        session.true_voltage_v.push_back(v_true);
        session.true_current_a.push_back(i_true);
        session.true_soc.push_back(soc);
        session.true_regimes.push_back(common::SegmentType::CHARGE);

        // Sensor Corruption
        double v_meas = v_true;
        double i_meas = i_true;
        bool is_spk = false;

        if (add_noise) {
            // Shunt sensor offset drift & gain error
            i_meas = (i_true * config_.current_gain_error) + config_.current_offset_a + i_noise(rng);
            v_meas += v_noise(rng);

            // Sporadic impulse noise / spikes
            if (uniform(rng) < config_.outlier_probability) {
                const double sgn = (spike_sign(rng) >= 0.0) ? 1.0 : -1.0;
                v_meas += sgn * config_.outlier_magnitude_v;
                is_spk = true;
            }
        }

        session.is_outlier.push_back(is_spk);
        session.is_interpolated.push_back(false);

        // Add telemetry sample
        common::Sample sample{
            .timestamp_s = t,
            .voltage_v = v_meas,
            .current_a = i_meas,
            .temperature_c = temp_c,
            .cell_voltages_v = std::nullopt
        };
        session.telemetry.samples.push_back(sample);

        t += dt;
    }

    return session;
}

GroundTruthSession TelemetryGenerator::generateFullSession(uint32_t seed, double initial_soc) {
    GroundTruthSession session;
    session.telemetry.session_id = "synth_session_full_001";
    session.telemetry.pack_id = "pack_LFP_48V_30Ah";
    session.telemetry.chemistry = config_.chemistry;
    session.telemetry.nominal_capacity_ah = config_.nominal_capacity_ah;

    std::mt19937 rng(seed);
    std::normal_distribution<double> v_noise(0.0, config_.voltage_noise_std_v);
    std::normal_distribution<double> i_noise(0.0, config_.current_noise_std_a);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    std::uniform_real_distribution<double> drive_cur(-25.0, -5.0);

    const double dt = config_.sampling_interval_s;
    const double q_total_as = config_.nominal_capacity_ah * 3600.0;
    const double r0_pack = 0.0018 * static_cast<double>(config_.series_cells);
    const double r1_pack = 0.0012 * static_cast<double>(config_.series_cells);
    const double tau_rc = 25.0;

    double soc = initial_soc;
    double t = 1700000000.0; // Realistic Unix epoch
    double v_rc = 0.0;
    double temp_c = config_.ambient_temp_c;

    // Operational protocol:
    // 1. Pre-Drive Rest: 300 seconds (5 min)
    // 2. Drive Cycle: 1200 seconds (20 min)
    // 3. Post-Drive Rest: 400 seconds
    // 4. CC Charge: 1800 seconds (30 min at 15A)
    // 5. Post-Charge Rest: 300 seconds

    struct Stage {
        common::SegmentType type;
        int duration_s;
        double target_current_a;
        bool is_drive;
    };

    std::vector<Stage> stages = {
        {common::SegmentType::REST,      300,   0.0, false},
        {common::SegmentType::DISCHARGE, 1200, -12.0, true},
        {common::SegmentType::REST,      400,   0.0, false},
        {common::SegmentType::CHARGE,    1800,  15.0, false},
        {common::SegmentType::REST,      300,   0.0, false}
    };

    for (const auto& stage : stages) {
        for (int step = 0; step < stage.duration_s; ++step) {
            double i_true = stage.target_current_a;
            if (stage.is_drive) {
                // Fluctuating drive cycle current
                if (step % 30 < 5) {
                    i_true = 0.0; // Red light / pause
                } else if (step % 60 < 20) {
                    i_true = drive_cur(rng); // Acceleration
                } else {
                    i_true = -8.0; // Cruise
                }
            }

            // Dynamics update
            soc += (i_true * dt) / q_total_as;
            soc = std::clamp(soc, 0.05, 0.98);
            v_rc = v_rc * std::exp(-dt / tau_rc) + (i_true * r1_pack) * (1.0 - std::exp(-dt / tau_rc));

            const double ocv_pack = cellOcv(soc) * static_cast<double>(config_.series_cells);
            const double v_true = ocv_pack + i_true * r0_pack + v_rc;

            // Thermal update
            const double power_loss = i_true * i_true * (r0_pack + r1_pack);
            temp_c += dt * (power_loss * 0.001 - (temp_c - config_.ambient_temp_c) * 0.005);

            // Record ground truth
            session.true_voltage_v.push_back(v_true);
            session.true_current_a.push_back(i_true);
            session.true_soc.push_back(soc);
            session.true_regimes.push_back(stage.type);

            // Injected sensor imperfections
            double v_meas = v_true + v_noise(rng);
            double i_meas = (i_true * config_.current_gain_error) + config_.current_offset_a + i_noise(rng);
            bool is_spk = false;

            if (uniform(rng) < config_.outlier_probability) {
                v_meas += ((uniform(rng) > 0.5) ? 1.0 : -1.0) * config_.outlier_magnitude_v;
                is_spk = true;
            }

            session.is_outlier.push_back(is_spk);
            session.is_interpolated.push_back(false);

            // Check for dropped packet (simulate timestamp gap)
            if (uniform(rng) < config_.packet_drop_prob && stage.type != common::SegmentType::REST) {
                // Drop packet: skip adding to telemetry.samples, but increment time
                t += dt;
                continue;
            }

            common::Sample sample{
                .timestamp_s = t,
                .voltage_v = v_meas,
                .current_a = i_meas,
                .temperature_c = temp_c,
                .cell_voltages_v = std::nullopt
            };
            session.telemetry.samples.push_back(sample);

            t += dt;
        }
    }

    return session;
}

} // namespace ev::battery::testing
