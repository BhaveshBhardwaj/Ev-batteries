#include <ev/battery/ingestion/ingestion.hpp>
#include <ev/battery/output/certificate.hpp>
#include <ev/battery/output/sha256.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ev::battery;

namespace {

void printHelp() {
    std::cout << R"(
EV Battery Health Certification Engine - CLI (BPAN Tier 3)
Usage:
  ev_battery_cli --input <file.csv|file.json> [options]

Required:
  -i, --input <path>        Path to telemetry file (.csv or .json)

Options:
  --bpan-id <id>            Battery Pack Aadhaar Number (default: IN-BPAN-2W-LFP-00941)
  --serial <sn>             OEM Serial Number (default: OEM-2026-X)
  --chemistry <LFP|NMC>     Cell chemistry (default: LFP)
  --category <2W|3W|SWAP>   Vehicle category (default: 2W)
  --capacity <Ah>           Nominal pack capacity in Ah (default: 30.0)
  --voltage <V>             Nominal pack voltage in V (default: 51.2)
  --cells <N>               Series cell count (default: 16)
  --efc <cycles>            Lifetime Equivalent Full Cycles (default: 120.0)
  -f, --format <format>     Output format: json, report, or both (default: both)
  -o, --out <dir>           Output destination directory (default: .)
  -h, --help                Display this help documentation
)" << std::endl;
}

std::string readFileContent(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace

int main(int argc, char* argv[]) {
    std::string input_path;
    output::BpanMetadata metadata;
    metadata.bpan_id = "IN-BPAN-2W-LFP-00941";
    metadata.serial_number = "OEM-2026-X";
    metadata.chemistry = "LFP";
    metadata.vehicle_category = "2W";
    metadata.nominal_capacity_ah = 30.0;
    metadata.nominal_voltage_v = 51.2;
    metadata.series_cells = 16;
    double lifetime_efc = 120.0;
    std::string format = "both";
    std::string out_dir = ".";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printHelp();
            return 0;
        } else if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            input_path = argv[++i];
        } else if (arg == "--bpan-id" && i + 1 < argc) {
            metadata.bpan_id = argv[++i];
        } else if (arg == "--serial" && i + 1 < argc) {
            metadata.serial_number = argv[++i];
        } else if (arg == "--chemistry" && i + 1 < argc) {
            metadata.chemistry = argv[++i];
        } else if (arg == "--category" && i + 1 < argc) {
            metadata.vehicle_category = argv[++i];
        } else if (arg == "--capacity" && i + 1 < argc) {
            metadata.nominal_capacity_ah = std::stod(argv[++i]);
        } else if (arg == "--voltage" && i + 1 < argc) {
            metadata.nominal_voltage_v = std::stod(argv[++i]);
        } else if (arg == "--cells" && i + 1 < argc) {
            metadata.series_cells = std::stoi(argv[++i]);
        } else if (arg == "--efc" && i + 1 < argc) {
            lifetime_efc = std::stod(argv[++i]);
        } else if ((arg == "-f" || arg == "--format") && i + 1 < argc) {
            format = argv[++i];
        } else if ((arg == "-o" || arg == "--out") && i + 1 < argc) {
            out_dir = argv[++i];
        }
    }

    if (input_path.empty()) {
        std::cerr << "Error: Missing required argument --input <path>\n";
        printHelp();
        return 1;
    }

    std::cout << "\n================================================================================\n";
    std::cout << "  EV BATTERY HEALTH VALUATOR & BPAN TIER 3 CERTIFICATION ENGINE\n";
    std::cout << "  Government of India / NITI Aayog Digital Battery Passport Authority\n";
    std::cout << "================================================================================\n";
    std::cout << "Reading input telemetry: " << input_path << "\n";

    std::string content;
    try {
        content = readFileContent(input_path);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }

    common::Result<common::InternalTimeSeries> ingest_res;
    if (input_path.ends_with(".json")) {
        ingest_res = ingestion::parseJson(content);
    } else {
        ingest_res = ingestion::parseCsv(content);
    }

    if (!ingest_res) {
        std::cerr << "[ERROR] Ingestion failed: " << ingest_res.error().message << "\n";
        return 1;
    }

    const auto& series = *ingest_res;
    std::cout << "Ingested " << series.samples.size() << " samples.\n";
    std::cout << "Executing complete 9-module electrochemical intelligence pipeline...\n\n";

    auto cert_res = output::generateHealthCertificate(series, metadata, lifetime_efc);
    if (!cert_res) {
        std::cerr << "[ERROR] Pipeline execution failed: " << cert_res.error().message << "\n";
        return 1;
    }

    const auto& cert = *cert_res;

    // Print summary to terminal
    std::string report = output::generateHumanReadableReport(cert);
    std::cout << report << "\n";

    // Write output files
    std::filesystem::create_directories(out_dir);

    if (format == "json" || format == "both") {
        std::string json_str = output::serializeCertificateToJson(cert);
        std::string json_file = out_dir + "/" + cert.metadata.bpan_id + "_certificate.json";
        std::ofstream jf(json_file);
        jf << json_str;
        std::cout << "[SUCCESS] Wrote BPAN Tier 3 JSON passport to: " << json_file << "\n";
    }

    if (format == "report" || format == "both") {
        std::string report_file = out_dir + "/" + cert.metadata.bpan_id + "_inspection_report.txt";
        std::ofstream rf(report_file);
        rf << report;
        std::cout << "[SUCCESS] Wrote printable inspection certificate to: " << report_file << "\n";
    }

    return cert.is_certified ? 0 : 2;
}
