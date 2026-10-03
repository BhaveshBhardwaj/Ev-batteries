#include <gtest/gtest.h>
#include <ev/battery/output/certificate.hpp>
#include <ev/battery/output/sha256.hpp>
#include "../synthetic/telemetry_generator.hpp"

#include <chrono>

using namespace ev::battery;
namespace synth = ev::battery::testing;

TEST(Sha256Test, NistReferenceStandardVectors) {
    // 1. Empty string test vector
    EXPECT_EQ(output::computeSha256(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    // 2. Standard NIST single-block test vector: "abc"
    EXPECT_EQ(output::computeSha256("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // 3. NIST multi-block test vector: 448-bit string
    EXPECT_EQ(output::computeSha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(CertificateTest, TamperDetectionAvalancheEffect) {
    output::HealthCertificate cert;
    cert.metadata.bpan_id = "IN-BPAN-2W-LFP-00941";
    cert.evaluation_timestamp_utc = "2026-10-03T21:50:00Z";
    cert.certified_soh_pct = 85.50;
    cert.soh_lower_bound_pct = 83.10;
    cert.soh_upper_bound_pct = 87.90;
    cert.remaining_useful_life_cycles = 1350.0;
    cert.thermal_degradation_index = 1.35;
    cert.certification_decision = "CERTIFIED";

    std::string payload1 = cert.metadata.bpan_id + "|" +
                           cert.evaluation_timestamp_utc + "|85.50|[83.10,87.90]|1350.00|1.35|CERTIFIED";
    std::string hash1 = output::computeSha256(payload1);

    // Tampered payload: altering SoH by 0.1% from 85.50 to 85.51
    std::string payload2 = cert.metadata.bpan_id + "|" +
                           cert.evaluation_timestamp_utc + "|85.51|[83.10,87.90]|1350.00|1.35|CERTIFIED";
    std::string hash2 = output::computeSha256(payload2);

    EXPECT_NE(hash1, hash2);

    // Count differing characters between the two 64-char hashes
    size_t differing_chars = 0;
    for (size_t i = 0; i < hash1.size(); ++i) {
        if (hash1[i] != hash2[i]) {
            differing_chars++;
        }
    }
    // Cryptographic avalanche effect guarantees widespread alteration (> 40% chars differ)
    EXPECT_GT(differing_chars, 25);
}

TEST(CertificateTest, SerializationJsonAndHumanReadableReport) {
    output::HealthCertificate cert;
    cert.metadata.bpan_id = "IN-BPAN-2W-LFP-00123";
    cert.metadata.serial_number = "EXIDE-LFP-30AH-2026";
    cert.metadata.chemistry = "LFP";
    cert.metadata.vehicle_category = "2W";
    cert.metadata.nominal_capacity_ah = 30.0;
    cert.metadata.nominal_voltage_v = 51.2;
    cert.metadata.series_cells = 16;

    cert.certified_soh_pct = 92.40;
    cert.usable_capacity_ah = 27.72;
    cert.lifetime_efc = 150.0;
    cert.soh_lower_bound_pct = 90.10;
    cert.soh_upper_bound_pct = 94.70;
    cert.confidence_interval_width_pct = 4.60;
    cert.coverage_level = 0.90;
    cert.remaining_useful_life_cycles = 1850.0;
    cert.lli_percent = 7.10;
    cert.lam_percent = 5.80;
    cert.primary_degradation_mode = "BALANCED";
    cert.thermal_degradation_index = 1.25;
    cert.mean_temperature_c = 34.5;
    cert.max_temperature_c = 41.2;
    cert.sensor_offset_bias_a = 0.05;
    cert.sensor_gain_factor = 1.002;
    cert.is_certified = true;
    cert.certification_decision = "CERTIFIED";
    cert.decision_rationale = "High certainty valuation within distribution.";
    cert.evaluation_timestamp_utc = "2026-10-03T21:50:00Z";
    cert.canonical_payload = "IN-BPAN-2W-LFP-00123|2026-10-03T21:50:00Z|92.40|[90.10,94.70]|1850.00|1.25|CERTIFIED";
    cert.sha256_digital_signature = output::computeSha256(cert.canonical_payload);

    // 1. JSON Serialization
    std::string json_str = output::serializeCertificateToJson(cert);
    EXPECT_NE(json_str.find("\"bpan_tier\": 3"), std::string::npos);
    EXPECT_NE(json_str.find("\"bpan_id\": \"IN-BPAN-2W-LFP-00123\""), std::string::npos);
    EXPECT_NE(json_str.find("\"certified_soh_pct\": 92.40"), std::string::npos);
    EXPECT_NE(json_str.find("\"sha256_digital_signature\": \"" + cert.sha256_digital_signature + "\""), std::string::npos);

    // 2. Human Readable Report
    std::string report = output::generateHumanReadableReport(cert);
    EXPECT_NE(report.find("GOVERNMENT OF INDIA"), std::string::npos);
    EXPECT_NE(report.find("BPAN TIER 3 BATTERY HEALTH & VALUATION CERTIFICATE"), std::string::npos);
    EXPECT_NE(report.find("IN-BPAN-2W-LFP-00123"), std::string::npos);
    EXPECT_NE(report.find(cert.sha256_digital_signature), std::string::npos);
}

TEST(CertificateTest, EndToEndSimToRealHealthCertificate) {
    // 1. Synthesize full multi-regime Indian commercial fleet session
    synth::GeneratorConfig cfg;
    cfg.chemistry = "LFP";
    cfg.nominal_capacity_ah = 30.0;
    cfg.series_cells = 16;
    cfg.ambient_temp_c = 42.0;
    cfg.current_offset_a = 0.50;
    cfg.current_gain_error = 1.01;
    cfg.voltage_noise_std_v = 0.03;
    cfg.current_noise_std_a = 0.10;
    cfg.outlier_probability = 0.01;
    cfg.packet_drop_prob = 0.01;

    synth::TelemetryGenerator gen(cfg);
    auto gt = gen.generateFullSession(42, 0.40);
    ASSERT_FALSE(gt.telemetry.samples.empty());

    output::BpanMetadata metadata;
    metadata.bpan_id = "IN-BPAN-2W-LFP-99001";
    metadata.serial_number = "TATAPWR-LFP-2026-X";
    metadata.chemistry = "LFP";
    metadata.vehicle_category = "2W";
    metadata.nominal_capacity_ah = 30.0;
    metadata.nominal_voltage_v = 51.2;
    metadata.series_cells = 16;

    auto start_time = std::chrono::steady_clock::now();

    auto cert_res = output::generateHealthCertificate(gt.telemetry, metadata, 120.0);
    ASSERT_TRUE(cert_res.has_value());

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    const auto& cert = *cert_res;

    // Verify metadata
    EXPECT_EQ(cert.metadata.bpan_id, "IN-BPAN-2W-LFP-99001");
    EXPECT_EQ(cert.metadata.chemistry, "LFP");
    EXPECT_EQ(cert.lifetime_efc, 120.0);

    // Verify certified health ranges
    EXPECT_GE(cert.certified_soh_pct, 70.0);
    EXPECT_LE(cert.certified_soh_pct, 105.0);
    EXPECT_GT(cert.usable_capacity_ah, 20.0);
    EXPECT_LE(cert.usable_capacity_ah, 32.0);

    // Verify uncertainty intervals
    EXPECT_LE(cert.soh_lower_bound_pct, cert.certified_soh_pct);
    EXPECT_GE(cert.soh_upper_bound_pct, cert.certified_soh_pct);
    EXPECT_GT(cert.confidence_interval_width_pct, 0.0);

    // Verify thermal metrics
    EXPECT_GT(cert.thermal_degradation_index, 1.0);
    EXPECT_GE(cert.mean_temperature_c, 30.0);

    // Verify cryptographic immutability
    EXPECT_EQ(cert.sha256_digital_signature.size(), 64);
    EXPECT_EQ(output::computeSha256(cert.canonical_payload), cert.sha256_digital_signature);

    // Verify execution speed: complete 9-module chain in under 150 ms
    EXPECT_LT(elapsed_ms, 150);
}

TEST(CertificateTest, Gate2HardwareFaultSafetyAbstention) {
    // Synthesize session with extreme current sensor zero-drift (|alpha| = 2.1 A > 1.5 A limit)
    synth::GeneratorConfig cfg;
    cfg.current_offset_a = 2.10; // Hardware fault
    cfg.nominal_capacity_ah = 30.0;

    synth::TelemetryGenerator gen(cfg);
    auto gt = gen.generateFullSession(42, 0.40);

    output::BpanMetadata metadata;
    metadata.bpan_id = "IN-BPAN-2W-FAULT-001";
    metadata.nominal_capacity_ah = 30.0;

    auto cert_res = output::generateHealthCertificate(gt.telemetry, metadata, 50.0);
    ASSERT_TRUE(cert_res.has_value());

    const auto& cert = *cert_res;

    // Gate 2 must trigger safety abstention
    EXPECT_FALSE(cert.is_certified);
    EXPECT_EQ(cert.certification_decision, "AUDIT_REQUIRED");
    EXPECT_NE(cert.decision_rationale.find("Gate 2"), std::string::npos);

    // Even though audit is required, the audit certificate must be signed
    EXPECT_EQ(cert.sha256_digital_signature.size(), 64);
    EXPECT_EQ(output::computeSha256(cert.canonical_payload), cert.sha256_digital_signature);
}
