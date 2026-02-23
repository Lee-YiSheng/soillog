import struct
import csv
import os

# --- CALIBRATION DATA ---
# Use the values you recorded during your "Air/Water" test.
# Higher = Dry, Lower = Wet for these resistive sensors.
AIR_VALUE = 4095   # 0% Moisture
WATER_VALUE = 1000 # 100% Moisture (adjust based on your test)

INPUT_FILE = "cacao_data_raw.bin"
OUTPUT_FILE = "cacao_cleaned_data.csv"
STRUCT_FMT = "<IH" # 4 bytes uint32, 2 bytes uint16
STRUCT_SIZE = 6

def calculate_moisture(raw):
    if raw >= AIR_VALUE: return 0.0
    if raw <= WATER_VALUE: return 100.0
    # Linear interpolation
    percent = (AIR_VALUE - raw) / (AIR_VALUE - WATER_VALUE) * 100
    return round(percent, 2)

def main():
    if not os.path.exists(INPUT_FILE):
        print(f"Error: {INPUT_FILE} not found.")
        return

    with open(INPUT_FILE, "rb") as bin_file, open(OUTPUT_FILE, "w", newline='') as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(["Index", "Hour", "Raw_ADC", "Moisture_Percent"])

        count = 0
        while True:
            chunk = bin_file.read(STRUCT_SIZE)
            if len(chunk) < STRUCT_SIZE: break

            timestamp, raw_val = struct.unpack(STRUCT_FMT, chunk)

            # FILTER: Ignore empty flash (0xFF) and impossible data soup
            if timestamp == 0xFFFFFFFF or timestamp > 50000 or raw_val > 4095:
                continue

            percent = calculate_moisture(raw_val)
            writer.writerow([count, timestamp, raw_val, f"{percent}%"])
            count += 1

    print(f"Done! Decoded {count} valid records to {OUTPUT_FILE}")

if __name__ == "__main__":
    main()