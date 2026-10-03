import os
import urllib.request
import zipfile
import pandas as pd
from scipy.io import loadmat
import numpy as np

WORKSPACE_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
NASA_DIR = os.path.join(WORKSPACE_ROOT, "data", "reference", "nasa_expanded")

os.makedirs(NASA_DIR, exist_ok=True)

def download_and_extract_nasa_zip():
    zip_url = "https://phm-datasets.s3.amazonaws.com/NASA/5.+Battery+Data+Set.zip"
    zip_path = os.path.join(NASA_DIR, "Battery_Data_Set.zip")
    
    if not os.path.exists(zip_path):
        print(f"Downloading NASA Battery Dataset ZIP from {zip_url} (this might take a while)...")
        urllib.request.urlretrieve(zip_url, zip_path)
        print("Download complete.")
    else:
        print("ZIP file already exists.")

    print("Extracting datasets from ZIP (including nested zip archives)...")
    import io
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        for item in zip_ref.namelist():
            if item.startswith('__MACOSX'):
                continue
            if item.endswith('.zip'):
                print(f"  Unpacking nested archive: {item}")
                sub_data = zip_ref.read(item)
                with zipfile.ZipFile(io.BytesIO(sub_data)) as sub_zip:
                    for sub_item in sub_zip.namelist():
                        if sub_item.endswith('.mat') and not sub_item.startswith('__MACOSX'):
                            target_path = os.path.join(NASA_DIR, os.path.basename(sub_item))
                            if not os.path.exists(target_path):
                                with sub_zip.open(sub_item) as source, open(target_path, "wb") as target:
                                    target.write(source.read())
                                print(f"    Extracted {os.path.basename(sub_item)}")
            elif item.endswith('.mat'):
                target_path = os.path.join(NASA_DIR, os.path.basename(item))
                if not os.path.exists(target_path):
                    with zip_ref.open(item) as source, open(target_path, "wb") as target:
                        target.write(source.read())
                    print(f"    Extracted {os.path.basename(item)}")

def process_mat_files():
    mat_files = [f for f in os.listdir(NASA_DIR) if f.endswith('.mat')]
    for mat_file in mat_files:
        ds = mat_file.replace('.mat', '')
        mat_path = os.path.join(NASA_DIR, mat_file)
        
        print(f"Parsing {ds}...")
        try:
            mat = loadmat(mat_path)
            # The structure varies, but most standard ones like B0005 are under their own name.
            if ds not in mat:
                print(f"Key {ds} not in {mat_file}. Skipping.")
                continue
                
            b_data = mat[ds][0, 0]
            if 'cycle' not in b_data.dtype.names:
                print(f"No 'cycle' field in {ds}. Skipping.")
                continue
                
            cycles = b_data["cycle"][0]
        except Exception as e:
            print(f"Error parsing {ds}: {e}")
            continue
            
        charge_count = 0
        discharge_count = 0
        capacities = []

        for idx, c in enumerate(cycles):
            ctype = c["type"][0]
            if len(c["data"]) == 0 or len(c["data"][0]) == 0:
                continue
            data = c["data"][0, 0]
            
            if ctype == "charge":
                charge_count += 1
                if charge_count in (1, 50, 100):
                    try:
                        v = data["Voltage_measured"][0]
                        i = data["Current_measured"][0]
                        temp = data["Temperature_measured"][0]
                        t = data["Time"][0]
                        
                        df = pd.DataFrame({
                            "timestamp_s": t,
                            "voltage_v": v,
                            "current_a": i,
                            "temperature_c": temp
                        })
                        csv_path = os.path.join(NASA_DIR, f"{ds}_charge_cycle_{charge_count:03d}.csv")
                        df.to_csv(csv_path, index=False)
                    except ValueError:
                        pass
                    
            elif ctype == "discharge":
                discharge_count += 1
                cap = np.nan
                if "Capacity" in data.dtype.names and data["Capacity"].size > 0:
                    try:
                        cap = float(data["Capacity"][0, 0])
                    except (IndexError, TypeError, ValueError):
                        pass
                capacities.append({
                    "discharge_cycle": discharge_count,
                    "capacity_ah": cap
                })

        # Save capacity aging summary
        if capacities:
            cap_df = pd.DataFrame(capacities)
            summary_path = os.path.join(NASA_DIR, f"{ds}_capacity_fade.csv")
            cap_df.to_csv(summary_path, index=False)
            print(f"  Extracted capacity summary for {ds} -> {len(cap_df)} cycles")

if __name__ == "__main__":
    download_and_extract_nasa_zip()
    process_mat_files()
    print("All additional datasets processed.")
