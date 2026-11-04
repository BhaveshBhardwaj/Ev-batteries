"""
EV Battery Health Certification Engine - Production REST API Server
Bridges the web frontend and live IoT telematics directly to the C++23 native core.
Zero external dependencies (uses Python 3 standard library).
"""

import http.server
import socketserver
import os
import sys
import json
import subprocess
import tempfile
import urllib.parse
import mimetypes

PORT = 8080
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
FRONTEND_DIR = os.path.join(PROJECT_ROOT, "frontend")
CLI_BIN = os.path.join(PROJECT_ROOT, "build", "src", "apps", "cli", "Release", "ev_battery_cli.exe")

if not os.path.exists(CLI_BIN):
    CLI_BIN_DEBUG = os.path.join(PROJECT_ROOT, "build", "src", "apps", "cli", "Debug", "ev_battery_cli.exe")
    if os.path.exists(CLI_BIN_DEBUG):
        CLI_BIN = CLI_BIN_DEBUG

INDIAN_VEHICLE_PROFILES = {
    "ola_s1_lfp": {
        "name": "Ola S1 Pro / Air (Commercial Delivery)",
        "chemistry": "LFP",
        "series_cells": 16,
        "nominal_capacity_ah": 30.0,
        "nominal_voltage_v": 51.2,
        "vehicle_category": "2W",
        "default_bpan": "IN-BPAN-2W-LFP-OLA01"
    },
    "ather_450x_nmc": {
        "name": "Ather 450X Gen 3 (Urban Micro-Mobility)",
        "chemistry": "NMC",
        "series_cells": 14,
        "nominal_capacity_ah": 58.0,
        "nominal_voltage_v": 51.8,
        "vehicle_category": "2W",
        "default_bpan": "IN-BPAN-2W-NMC-ATH02"
    },
    "tvs_iqube_nmc": {
        "name": "TVS iQube Electric (Commuter Fleet)",
        "chemistry": "NMC",
        "series_cells": 14,
        "nominal_capacity_ah": 60.0,
        "nominal_voltage_v": 50.4,
        "vehicle_category": "2W",
        "default_bpan": "IN-BPAN-2W-NMC-TVS03"
    },
    "blusmart_3w_lfp": {
        "name": "BluSmart Commercial 3W / Mahindra Treo",
        "chemistry": "LFP",
        "series_cells": 16,
        "nominal_capacity_ah": 150.0,
        "nominal_voltage_v": 51.2,
        "vehicle_category": "3W",
        "default_bpan": "IN-BPAN-3W-LFP-BLU04"
    },
    "zypp_cargo_lfp": {
        "name": "Zypp Cargo Delivery Swap Pack",
        "chemistry": "LFP",
        "series_cells": 16,
        "nominal_capacity_ah": 32.0,
        "nominal_voltage_v": 51.2,
        "vehicle_category": "2W",
        "default_bpan": "IN-BPAN-2W-LFP-ZYP05"
    },
    "nasa_single_cell": {
        "name": "Laboratory Reference 18650 Single Cell (NASA PCoE)",
        "chemistry": "NMC",
        "series_cells": 1,
        "nominal_capacity_ah": 2.0,
        "nominal_voltage_v": 4.2,
        "vehicle_category": "LAB",
        "default_bpan": "IN-BPAN-LAB-NMC-B0005"
    }
}

class BatteryPassportHandler(http.server.BaseHTTPRequestHandler):
    
    def _send_json(self, data, status=200):
        body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/" or path == "":
            self.send_response(302)
            self.send_header("Location", "/frontend/")
            self.end_headers()
            return

        if path == "/api/status":
            self._send_json({
                "status": "ONLINE",
                "core_engine": "C++23 ev_battery_core",
                "cli_binary": CLI_BIN,
                "cli_ready": os.path.exists(CLI_BIN),
                "profiles": len(INDIAN_VEHICLE_PROFILES)
            })
            return

        if path == "/api/profiles":
            self._send_json(INDIAN_VEHICLE_PROFILES)
            return

        # Serve static frontend files
        if path.startswith("/frontend/"):
            rel_path = path[len("/frontend/"):]
            if not rel_path or rel_path == "":
                rel_path = "index.html"
            file_path = os.path.join(FRONTEND_DIR, rel_path)
            
            if os.path.isfile(file_path):
                mime, _ = mimetypes.guess_type(file_path)
                if not mime:
                    mime = "application/octet-stream"
                try:
                    with open(file_path, "rb") as f:
                        content = f.read()
                    self.send_response(200)
                    self.send_header("Content-Type", mime)
                    self.send_header("Content-Length", str(len(content)))
                    self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
                    self.send_header("Pragma", "no-cache")
                    self.send_header("Expires", "0")
                    self.send_header("Access-Control-Allow-Origin", "*")
                    self.end_headers()
                    self.wfile.write(content)
                    return
                except Exception as e:
                    self.send_error(500, f"Error reading file: {e}")
                    return

        self.send_error(404, "Endpoint not found")

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/api/certify":
            length = int(self.headers.get("Content-Length", 0))
            if length <= 0:
                self._send_json({"error": "Empty request body"}, 400)
                return

            content_type = self.headers.get("Content-Type", "")
            raw_body = self.rfile.read(length).decode("utf-8", errors="replace")
            query_params = urllib.parse.parse_qs(parsed.query)

            req = {}
            if "application/json" in content_type or raw_body.strip().startswith("{"):
                try:
                    req = json.loads(raw_body)
                except Exception as e:
                    self._send_json({"error": f"Invalid JSON payload: {e}"}, 400)
                    return
            else:
                # Direct raw CSV or JSON text passed in POST body
                req = {"telemetry_raw": raw_body}

            # Overlay query parameters if provided
            for k, vals in query_params.items():
                if vals:
                    req[k] = vals[0]

            telemetry_content = req.get("telemetry_csv") or req.get("telemetry_json") or req.get("telemetry_raw")
            if telemetry_content is None:
                self._send_json({"error": "Missing 'telemetry_csv', 'telemetry_json' or 'telemetry_raw' in request"}, 400)
                return

            is_json_telemetry = False
            if isinstance(telemetry_content, (list, dict)):
                telemetry_content = json.dumps(telemetry_content, indent=2)
                is_json_telemetry = True
            elif isinstance(telemetry_content, str):
                trimmed = telemetry_content.strip()
                is_json_telemetry = trimmed.startswith("[") or trimmed.startswith("{")
            else:
                self._send_json({"error": "Invalid telemetry content type"}, 400)
                return

            # Extract pack parameters with vehicle profile fallback
            profile_key = req.get("profile") or req.get("vehicle") or "ola_s1_lfp"
            prof = INDIAN_VEHICLE_PROFILES.get(profile_key, INDIAN_VEHICLE_PROFILES.get("ola_s1_lfp"))

            chemistry = (req.get("chemistry") or prof["chemistry"]).upper()
            cells = int(req.get("series_cells") or req.get("cells") or prof["series_cells"])
            capacity = float(req.get("nominal_capacity_ah") or req.get("nominal_capacity") or req.get("capacity") or prof["nominal_capacity_ah"])
            voltage = float(req.get("nominal_voltage_v") or req.get("nominal_voltage") or req.get("voltage") or prof["nominal_voltage_v"])
            category = req.get("vehicle_category") or req.get("category") or prof["vehicle_category"]
            bpan_id = req.get("bpan_id") or prof["default_bpan"]
            serial = req.get("serial_number") or "OEM-2026-IND"
            efc = float(req.get("lifetime_efc") or req.get("efc") or 120.0)

            ext = ".json" if is_json_telemetry else ".csv"

            # Execute C++ core CLI in isolated temp directory
            with tempfile.TemporaryDirectory() as temp_dir:
                input_file = os.path.join(temp_dir, f"input_telemetry{ext}")
                with open(input_file, "w", encoding="utf-8") as f:
                    f.write(telemetry_content)

                cmd = [
                    CLI_BIN,
                    "--input", input_file,
                    "--bpan-id", bpan_id,
                    "--serial", serial,
                    "--chemistry", chemistry,
                    "--category", category,
                    "--capacity", str(capacity),
                    "--voltage", str(voltage),
                    "--cells", str(cells),
                    "--efc", str(efc),
                    "--format", "both",
                    "--out", temp_dir
                ]

                try:
                    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
                    stdout = proc.stdout
                    stderr = proc.stderr
                    return_code = proc.returncode

                    cert_file = os.path.join(temp_dir, f"{bpan_id}_certificate.json")
                    report_file = os.path.join(temp_dir, f"{bpan_id}_inspection_report.txt")

                    cert_data = None
                    report_text = None

                    if os.path.exists(cert_file):
                        with open(cert_file, "r", encoding="utf-8") as f:
                            cert_data = json.load(f)

                    if os.path.exists(report_file):
                        with open(report_file, "r", encoding="utf-8") as f:
                            report_text = f.read()

                    self._send_json({
                        "success": (cert_data is not None),
                        "is_certified": cert_data.get("certification_status", {}).get("is_certified", False) if cert_data else False,
                        "return_code": return_code,
                        "certificate": cert_data,
                        "report": report_text,
                        "cli_stdout": stdout,
                        "cli_stderr": stderr
                    })

                except subprocess.TimeoutExpired:
                    self._send_json({"error": "C++ core processing timed out (>15s)"}, 504)
                except Exception as e:
                    self._send_json({"error": f"Execution failed: {e}"}, 500)
            return

        self.send_error(404, "Endpoint not found")

def run_server():
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), BatteryPassportHandler) as httpd:
        print(f"================================================================================")
        print(f"  BHARAT BATTERY PASSPORT TIER 3 - REST API SERVER & WEB RUNTIME")
        print(f"  Listening on http://localhost:{PORT}")
        print(f"  C++ Engine: {CLI_BIN}")
        print(f"================================================================================")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down server...")

if __name__ == "__main__":
    run_server()
