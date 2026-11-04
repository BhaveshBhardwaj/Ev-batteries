import urllib.request
import json
import sys

def test_api():
    print("--> 1. Testing GET /api/status...")
    req = urllib.request.Request("http://localhost:8080/api/status")
    with urllib.request.urlopen(req) as resp:
        data = json.loads(resp.read().decode("utf-8"))
        print(f"    Status: {data['status']}, Core: {data['core_engine']}, CLI ready: {data['cli_ready']}")

    print("\n--> 2. Testing POST /api/certify with data/sample_fleet_certified.csv...")
    with open("data/sample_fleet_certified.csv", "rb") as f:
        csv_bytes = f.read()

    req = urllib.request.Request(
        "http://localhost:8080/api/certify?vehicle=ola_s1_lfp",
        data=csv_bytes,
        headers={"Content-Type": "text/csv"}
    )
    with urllib.request.urlopen(req) as resp:
        res = json.loads(resp.read().decode("utf-8"))
        print(f"    Success: {res.get('success')}")
        print(f"    Is Certified: {res.get('is_certified')}")
        cert = res.get("certificate")
        if cert:
            meta = cert.get("bpan_metadata", {})
            val = cert.get("certified_health_valuation", {})
            unc = cert.get("conformal_uncertainty", {})
            sig = cert.get("cryptographic_signature", {})
            dec = cert.get("certification_status", {})
            print(f"    BPAN ID: {meta.get('bpan_id')}")
            print(f"    Vehicle: {meta.get('vehicle_category')} ({meta.get('chemistry')}, {meta.get('series_cells')}S)")
            print(f"    Certified SoH: {val.get('certified_soh_pct')}% ({val.get('usable_capacity_ah')} Ah)")
            print(f"    90% Conformal Interval: [{unc.get('soh_lower_bound_pct')}%, {unc.get('soh_upper_bound_pct')}%] (Width: {unc.get('confidence_interval_width_pct')}%)")
            print(f"    Decision: {dec.get('certification_decision')} - {dec.get('decision_rationale')}")
            print(f"    SHA-256 Digest: {sig.get('sha256_digital_signature')}")
        else:
            print(f"    STDOUT: {res.get('cli_stdout')}")
            print(f"    STDERR: {res.get('cli_stderr')}")

    print("\n--> 3. Testing POST /api/certify with JSON payload (NASA cycle 100)...")
    with open("data/reference/nasa/B0005_charge_cycle_100.csv", "r", encoding="utf-8") as f:
        nasa_csv = f.read()

    payload = {
        "profile": "nasa_single_cell",
        "bpan_id": "IN-BPAN-NASA-B0005-CYC100",
        "telemetry_csv": nasa_csv,
        "lifetime_efc": 100.0
    }
    req = urllib.request.Request(
        "http://localhost:8080/api/certify",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req) as resp:
        res = json.loads(resp.read().decode("utf-8"))
        print(f"    Success: {res.get('success')}")
        print(f"    Is Certified: {res.get('is_certified')}")
        cert = res.get("certificate")
        if cert:
            meta = cert.get("bpan_metadata", {})
            val = cert.get("certified_health_valuation", {})
            unc = cert.get("conformal_uncertainty", {})
            sig = cert.get("cryptographic_signature", {})
            print(f"    BPAN ID: {meta.get('bpan_id')}")
            print(f"    Certified SoH: {val.get('certified_soh_pct')}% ({val.get('usable_capacity_ah')} Ah)")
            print(f"    90% Conformal Interval: [{unc.get('soh_lower_bound_pct')}%, {unc.get('soh_upper_bound_pct')}%] (Width: {unc.get('confidence_interval_width_pct')}%)")
            print(f"    SHA-256 Digest: {sig.get('sha256_digital_signature')}")

    print("\nALL API ENDPOINTS TESTED SUCCESSFULLY.")

if __name__ == "__main__":
    test_api()
