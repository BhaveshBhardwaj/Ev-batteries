"""
Generates a realistic 16S LFP Indian 2W Commercial Fleet Telemetry dataset.
Operational profile: Rest (10m) -> Discharge (30m) -> Rest (15m) -> CC-CV Charge (60m) -> Rest (15m)
Nominal rating: 16S LFP, 51.2V nominal, 30.0 Ah capacity.
"""

import numpy as np
import pandas as pd
import json
import os

def cell_ocv_lfp(soc):
    z = np.clip(soc, 0.001, 0.999)
    base = 3.25
    slope = 0.12 * z
    lower_knee = -0.08 * np.exp(-35.0 * z)
    upper_knee = 0.28 * np.exp(45.0 * (z - 1.0))
    phase1 = 0.035 * np.tanh(25.0 * (z - 0.22))
    phase2 = 0.025 * np.tanh(30.0 * (z - 0.55))
    phase3 = 0.020 * np.tanh(35.0 * (z - 0.78))
    return base + slope + lower_knee + upper_knee + phase1 + phase2 + phase3

def generate_telemetry(seed=42):
    np.random.seed(seed)
    
    nominal_ah = 30.0
    series_cells = 16
    q_total_as = nominal_ah * 3600.0
    r0_pack = 0.0018 * series_cells # ~28.8 mOhm
    r1_pack = 0.0012 * series_cells
    tau_rc = 25.0
    
    ambient_temp_c = 38.0
    c_th = 4500.0 # Thermal capacitance J/K
    r_th = 0.45   # K/W passive air
    
    # State variables
    soc = 0.50
    v_rc = 0.0
    temp_c = ambient_temp_c
    
    # Sensor offsets (within Gate 2 limits)
    i_offset = 0.35 # +0.35A zero drift
    i_gain = 1.008  # +0.8% gain error
    
    records = []
    t_sec = 0.0
    
    # Segments definition: (type, duration_s, current_a)
    segments = [
        ("REST_1", 600, 0.0),
        ("DISCHARGE", 1800, -18.0), # 18A delivery discharge
        ("REST_2", 900, 0.0),
        ("CHARGE_CC", 2400, 15.0),  # 15A CC fast charge (~0.5C)
        ("REST_3", 600, 0.0)
    ]
    
    dt = 1.0 # 1 Hz
    
    for seg_name, duration, i_target in segments:
        steps = int(duration / dt)
        for _ in range(steps):
            # Dynamic current with slight drive ripples if discharging
            curr = i_target
            if i_target < -1.0:
                curr += np.random.normal(0, 0.8) # Traffic accel/braking
                
            # Coulomb counting state update
            dsoc = (curr * dt) / q_total_as
            soc = np.clip(soc + dsoc, 0.02, 0.98)
            
            # RC polarization
            dv_rc = (-v_rc / tau_rc + (curr * r1_pack) / tau_rc) * dt
            v_rc += dv_rc
            
            # OCV & True terminal voltage
            v_ocv = series_cells * cell_ocv_lfp(soc)
            v_term = v_ocv + curr * r0_pack + v_rc
            
            # Thermal model (Joule heating + ambient dissipation)
            p_loss = (curr ** 2) * (r0_pack + r1_pack)
            dtemp = ((p_loss - (temp_c - ambient_temp_c) / r_th) / c_th) * dt
            temp_c += dtemp
            
            # Sensor noise & corrupted telemetry
            v_noisy = v_term + np.random.normal(0, 0.015)
            i_sensor = (curr + i_offset) * i_gain + np.random.normal(0, 0.05)
            temp_sensor = temp_c + np.random.normal(0, 0.1)
            
            # Inject sporadic outlier (1 in 500)
            if np.random.rand() < 0.002:
                v_noisy += np.random.choice([-0.4, 0.4])
                
            # Occasional 1s packet drop (skip 1 step)
            if np.random.rand() < 0.005:
                t_sec += dt
                continue
                
            records.append({
                "timestamp_s": round(t_sec, 2),
                "voltage_v": round(float(v_noisy), 4),
                "current_a": round(float(i_sensor), 4),
                "temperature_c": round(float(temp_sensor), 2)
            })
            
            t_sec += dt

    df = pd.DataFrame(records)
    out_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "data"))
    csv_path = os.path.join(out_dir, "sample_fleet_telemetry.csv")
    json_path = os.path.join(out_dir, "sample_fleet_telemetry.json")
    
    df.to_csv(csv_path, index=False)
    print(f"Generated {len(df)} samples in {csv_path}")
    
    with open(json_path, "w") as f:
        json.dump(records, f, indent=2)
    print(f"Generated JSON dataset in {json_path}")

if __name__ == "__main__":
    generate_telemetry()

