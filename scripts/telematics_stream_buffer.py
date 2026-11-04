"""
Real-time Telematics Streaming Session Buffer & Ingestion Agent.
Listens to live 1 Hz IoT telemetry streams (MQTT / HTTP JSON chunks),
accumulates active charging and rest sessions, and triggers automated
BPAN Tier 3 certification when a charging mission is completed.
"""

import time
import json
import urllib.request
import urllib.error
import threading
from collections import deque

class TelematicsSessionAccumulator:
    def __init__(self, vehicle_id, profile="ola_s1_lfp", api_url="http://localhost:8080/api/certify"):
        self.vehicle_id = vehicle_id
        self.profile = profile
        self.api_url = api_url
        self.buffer = []
        self.state = "IDLE" # IDLE, CHARGING, RESTING
        self.charge_start_time = None
        self.last_charge_time = None
        self.rest_counter_s = 0

    def ingest_sample(self, timestamp_s, voltage_v, current_a, temp_c):
        sample = {
            "timestamp_s": timestamp_s,
            "voltage_v": voltage_v,
            "current_a": current_a,
            "temperature_c": temp_c
        }
        self.buffer.append(sample)

        # State machine for charging session detection
        if current_a > 1.5:
            # Active charging
            if self.state != "CHARGING":
                print(f"[{self.vehicle_id}] Charging session STARTED at t={timestamp_s:.1f}s (I={current_a:.1f}A)")
                self.state = "CHARGING"
                self.charge_start_time = timestamp_s
            self.last_charge_time = timestamp_s
            self.rest_counter_s = 0

        elif abs(current_a) <= 0.5:
            # Rest regime
            if self.state == "CHARGING":
                self.rest_counter_s += 1
                # If resting for > 60 consecutive seconds post-charge, trigger certification
                if self.rest_counter_s >= 60:
                    print(f"[{self.vehicle_id}] Charging session COMPLETED (60s rest settled). Triggering certification...")
                    self.flush_and_certify()
                    self.state = "IDLE"

    def flush_and_certify(self):
        if len(self.buffer) < 50:
            print(f"[{self.vehicle_id}] Insufficient samples ({len(self.buffer)}) to certify. Discarding buffer.")
            self.buffer.clear()
            return

        print(f"[{self.vehicle_id}] Submitting {len(self.buffer)} samples to BPAN Tier 3 Engine...")
        
        # Format payload
        payload = {
            "profile": self.profile,
            "bpan_id": f"IN-BPAN-{self.vehicle_id.upper()}",
            "telemetry_json": self.buffer
        }

        try:
            req_data = json.dumps(payload).encode("utf-8")
            req = urllib.request.Request(
                self.api_url,
                data=req_data,
                headers={"Content-Type": "application/json"}
            )
            with urllib.request.urlopen(req, timeout=10) as resp:
                res = json.loads(resp.read().decode("utf-8"))
                if res.get("success"):
                    cert = res["certificate"]
                    print("\n" + "="*70)
                    print(f"  OFFICIAL BPAN TIER 3 CERTIFICATE ISSUED FOR {self.vehicle_id}")
                    print("="*70)
                    print(f"  BPAN ID:       {cert['bpan_metadata']['bpan_id']}")
                    print(f"  Certified SoH: {cert['certified_health_valuation']['certified_soh_pct']}%")
                    print(f"  Usable Cap:    {cert['certified_health_valuation']['usable_capacity_ah']} Ah")
                    print(f"  90% Conf Band: [{cert['conformal_uncertainty']['soh_lower_bound_pct']}%, {cert['conformal_uncertainty']['soh_upper_bound_pct']}%]")
                    print(f"  Status:        [{cert['certification_status']['certification_decision']}]")
                    print(f"  SHA-256 Seal:  {cert['cryptographic_signature']['sha256_digital_signature'][:24]}...")
                    print("="*70 + "\n")
                else:
                    print(f"[{self.vehicle_id}] Certification error: {res.get('cli_stderr')}")
        except Exception as e:
            print(f"[{self.vehicle_id}] Failed to reach API server: {e}")
            
        self.buffer.clear()

def simulate_live_iot_stream():
    """Simulates a real 1 Hz telematics transmission from an Ola S1 delivery scooter."""
    print("Initializing Live IoT Telematics Stream for Vehicle: OLA-S1-FLEET-4491...")
    acc = TelematicsSessionAccumulator("OLA-S1-FLEET-4491", profile="ola_s1_lfp")

    # Simulate 5-minute pre-charge rest -> 10-minute CC charge -> 2-minute post-charge rest
    t = 0.0
    soc = 0.40
    print("[STREAM] Transmitting pre-charge rest telemetry (5 minutes at 1 Hz)...")
    for _ in range(60): # 60s for demo
        acc.ingest_sample(t, 51.2 + 0.005, 0.02, 36.5)
        t += 1.0

    print("[STREAM] Transmitting 15A Constant-Current fast charge telemetry...")
    for _ in range(180): # 180s charge
        soc += (15.0 * 1.0) / (30.0 * 3600.0)
        v = 51.2 + 2.5 * (soc - 0.40) + 0.45
        acc.ingest_sample(t, v, 15.02, 38.0)
        t += 1.0

    print("[STREAM] Charging finished. Transmitting post-charge rest and thermal cooldown...")
    for _ in range(70): # 70s rest (triggers the 60s threshold)
        acc.ingest_sample(t, 52.8 - 0.002, 0.01, 37.8)
        t += 1.0

if __name__ == "__main__":
    simulate_live_iot_stream()

