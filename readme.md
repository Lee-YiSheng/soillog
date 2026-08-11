This node is an ultra-low-power, offline soil logging system built around the ESP32-C3 SuperMini (RISC-V). It operates on 2x AA Li-FeS2 batteries wired directly to the 3.3V power rail, using deep-sleep state management and internal SPIFFS storage to maximize battery longevity.

# Install esptool
pip install esptool

# Flash using the generated arguments
esptool.py --chip esp32 -p /dev/ttyUSB0 write_flash @flasher_args.json

or 
idf.py -p /dev/cu.usbserial-0001

idf.py -p /dev/cu.usbserial-0001 -b 115200 monitor

idf.py -p /dev/cu.usbserial-0001 -b 115200 flash monitor

idf.py -p /dev/cu.usbmodem101 flash


idf.py -p /dev/cu.usbmodem101 -b 115200 flash monitor

to see monitor

ls /dev/cu.*
to list whihc port (macos)

# to read flash

esptool.py -p /dev/cu.usbserial-0001 -b 460800 read_flash 0x110000 0x200000 cacao_data_raw.bin

or 

`
esptool.py -p /dev/cu.usbserial-0001 -b 115200 read_flash 0x110000 0x200000 cacao_data_raw.bin
`
# to clear flash
idf.py erase-flash

# to calibrate
change cmake target
```
idf.py fullclean && idf.py -p /dev/cu.usbmodem101 build flash monitor
```

# to convert bin to csv

```
python3 -c '
import struct

with open("cacao_data_raw.bin", "rb") as f:
    data = f.read()

record_size = 6  # 4-byte timestamp_hour + 2-byte moisture_raw

print("Hour_Timestamp,Raw_ADC")
found = 0
for i in range(0, len(data) - record_size + 1):
    hour, raw_adc = struct.unpack("<IH", data[i:i+record_size])
    if hour != 0xFFFFFFFF and 0 < hour < 100000 and 0 < raw_adc <= 4095:
        print(f"{hour},{raw_adc}")
        found += 1

if found == 0:
    print("# No valid binary records detected in this partition window.")
'
```

or 

`
python3 parse_data.py
`

hardware tested on:
Chipset: ESP32-C3 super Mini 

Processor: Dual-Core Xtensa® 32-bit LX6 CPU

Clock Speed: Up to 240MHz

Flash Memory: 4MB

RAM: 520KB SRAM

https://kuriosity.sg/products/soil-moisture-sensor
soil sensor example LM393 comparator


# Partition,Type,Size,Start Offset (Calculation)
nvs,data,0x4000 (16 KB),0x9000 (Default starting point)
otadata,data,0x2000 (8 KB),0xD000
phy_init,data,0x1000 (4 KB),0xF000
factory,app,0x100000 (1 MB),0x10000
storage,data,0x200000 (2 MB),0x110000






## 🔌 Hardware Wiring & Setup

This logging system uses a standard analog soil moisture sensor. To maximize the lifespan of the CR123A battery and prevent rapid galvanic corrosion on the sensor probes in the high-humidity cacao farm environment, the sensor is **not** connected to a constant 3.3V power supply. 

Instead, the sensor is powered dynamically via a standard GPIO pin. The ESP32 wakes up, supplies power for just a few milliseconds to take a reading, and then cuts the power before returning to deep sleep.






### Pin Mapping

| Soil Sensor Pin | ESP32 Pin | Purpose |
| :--- | :--- | :--- |
| **VCC / +** | **GPIO 25** | **Dynamic Power:** Toggled HIGH (`1`) only during the active reading window. |
| **GND / -** | **GND** | **Common Ground:** Connect to any available GND pin on the ESP32. |
| **A0 / SIG** | **GPIO 34** | **Analog Signal:** Reads the voltage drop across the soil. Mapped internally to `ADC1_CH6`. |



### Design Notes

Why ADC1? The ESP32-C3 utilizes ADC1 for analog reads while leaving system peripherals clear. By routing the sensor signal to GPIO 0 (ADC1_CH0), we ensure that concurrent Bluetooth LE radio broadcasts during the debug window will not interfere with or corrupt soil moisture measurements.

We are utilizing standard hookup/stranded copper wire for the probe run. Ensure all extension solder joints are fully insulated with heat-shrink tubing and silicone sealant to prevent short circuits from soil moisture.

How the Reed Switch Works
The Normally Open (NO) magnetic reed switch acts as a zero-power physical trigger for wireless field debugging.

Default State (99.9% of the time): The switch contacts inside the glass capsule remain physically separated. No current should be flowing through whne idling.

Swiping a magnet against the designated marker on the outside of the case physically closes the contacts, pulling GPIO 2 down to GND. This hardware interrupt immediately wakes the ESP32-C3 from deep sleep and triggers a 60-second BLE Debug & Data Retrieval Window.

## how to get data
You can retrieve logged soil data via Bluetooth LE directly in the field, or via Serial Download Mode on your bench.

Option A: Wireless Retrieval via Bluetooth (In the Field)
Open nRF Connect or LightBlue on your mobile device.

Hold a magnet over the reed switch marker on the sealed enclosure.

The board wakes up and advertises as IEx_Tree_1 for 60 seconds.

Connect to the node and open the GATT UART Characteristic to read/download the recent CSV data payload.

Option B: Wired Serial Retrieval (Bench Testing)
To execute a manual bench dump over a wired connection:

Flash the logging firmware to the ESP32-C3 SuperMini.

Open your serial monitor (VS Code Serial Monitor or idf.py monitor).

Press the RESET button on the board.

Immediately press and hold the BOOT button down.

Watch the serial monitor. Wait until you see *** DOWNLOAD MODE DETECTED *** and the full CSV printout before releasing the BOOT button.

Sensor Calibration Procedure (CRITICAL)
⚠️ DO NOT SKIP: Perform the analog soil sensor calibration ONLY AFTER your standard extension wires are permanently soldered to the probe and board.

Why post-soldering calibration is required:
Resistive sensors determine moisture by measuring electrical resistance across the soil. Standard copper wires add inherent resistance to the circuit proportional to their length. If you calibrate using short factory leads and then extend the wiring run, the ESP32-C3 will read the added copper wire resistance as part of the soil—falsely reporting that the tree is in bone-dry soil.

Calibration Protocol:
Air Baseline (0% Moisture / Dry Bound):

Hold the dry, fully soldered probe in the air (ensure metal prongs touch nothing).

Wake the board and record the raw ADC value from the serial/BLE output (e.g., 4095).

Water Baseline (100% Moisture / Wet Bound):

Submerge the probe prongs up to the plastic header line in a glass of tap water.

Record the raw ADC value from the serial/BLE output (e.g., 1250).

Firmware Map:

Update the firmware mapping parameters with your recorded values:
map(raw_adc, DRY_VAL, WET_VAL, 0, 100)

## debug
Step 1: Force "Manual Bootloader / Download Mode"
This forces the C3's internal ROM to bypass your current code and present its built-in USB interface directly to Windows.Plug the SuperMini into your Windows PC via USB.Press and hold the BOOT button on the SuperMini.While continuing to hold BOOT, press and release the RST (Reset) button once.  Release the BOOT button.  


#  Sensor Calibration & Hardware Profiling Guide

Before deploying an ESP32-C3 node to the field, each unit must undergo:
1. **MAC Address Retrieval** for unique hardware registration.
2. **Sensor Calibration** to establish dry ($V_{dry}$) and wet ($V_{wet}$) raw ADC baselines.

To prevent flash memory wear and bypass deep-sleep intervals during profiling, calibration is performed using a dedicated firmware target (`calibration_main.c`) that samples at rapid 1-second intervals over USB Serial.

---

## 🔍 Section 1: Retrieving the Board MAC Address

Each ESP32-C3 SuperMini has a unique, factory-burned 48-bit IEEE MAC address stored in eFuse. You can retrieve this address using either method below:

### Method A: Using `esptool.py` (Fastest - No Flashing Required)
With your ESP32-C3 plugged in via USB, run the following command in your terminal:

```bash
esptool.py -p /dev/cu.usbmodem101 read_mac
```

Example Console Output:

```
esptool.py v4.7.0
Serial port /dev/cu.usbmodem101
Connecting...
Detecting chip type... ESP32-C3
MAC: f4:2d:c9:6b:19:dc
Method B: Via Calibration Serial Output
The calibration_main.c script automatically reads the internal factory eFuse MAC on startup and prints it directly to the serial monitor:
```

==================================================
 [CACAO_LOGGER] CALIBRATION & HARDWARE PROFILING
 Target MAC Address : f4:2d:c9:6b:19:dc
==================================================
🛠️ Section 2: Sensor Calibration Procedure

Step 1: Switch to Calibration Firmware
By default, the build configuration targets the production logger (main/main.c). Switch to the calibration target in main/CMakeLists.txt:

Open main/CMakeLists.txt.

Toggle the active source file to calibration_main.c:

CMake
# main/CMakeLists.txt

# --- PRODUCTION BUILD (DISABLED) ---
# idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")

# --- CALIBRATION BUILD (ACTIVE) ---
idf_component_register(SRCS "calibration_main.c" INCLUDE_DIRS ".")
Step 2: Flash and Monitor Calibration
Plug the node into your computer via USB-C.

Build, flash, and open the monitor terminal:

Bash
idf.py -p /dev/cu.usbmodem101 build flash monitor
The terminal will print the MAC address and stream raw ADC samples every 1000 ms:

Plaintext
[CALIBRATION] Sample #12 | Raw ADC: 4095 | Voltage: 3300 mV
[CALIBRATION] Sample #13 | Raw ADC: 4095 | Voltage: 3300 mV
Step 3: Record Boundary Values
Air Baseline (Air_Value_Raw)

Suspend the soil moisture sensor prongs completely in free air. Ensure prongs are clean and dry.

Allow the reading to stabilize for ~10 seconds.

Record the average Raw ADC value (typically 4095 for an uncalibrated/disconnected probe or floating high rail).

Water Baseline (Water_Value_Raw)

Submerge the probe prongs in a container of tap water up to the maximum fill line (do NOT submerge the top circuit components).

Allow the reading to stabilize for ~10 seconds.

Record the average Raw ADC value (typically 1000 to 1500).

📋 Section 3: Calibration Manifest (calibration.csv)
Maintain a calibration.csv file in the root of your project repository to map unit IDs, hardware MAC addresses, field assignment locations, and calibration thresholds.

Standard Format Structure:
Code snippet
Unit_ID,MAC_Address,Deployment_Location,Air_Value_Raw,Water_Value_Raw,Status
Sensor_01,f4:2d:c9:6b:19:dc,Tree_A,4095,1534,Pending
Sensor_02,a4:f0:0f:67:47:00,Tree_B,4095,1064,Pending
Sensor_03,a4:f0:0f:5f:b0:54,Tree_C,4095,1125,Pending
Sensor_04,a4:f0:0f:5e:f0:08,Tree_D,4095,1138,Pending
Sensor_05,a4:f0:0f:5f:49:78,Tree_E,4095,1044,Pending
🚀 Section 4: Return to Production Mode
Once hardware profiling and logging in calibration.csv are complete:

Update ADC_DRY and ADC_WET values in main/main.c for your target board:

C
#define ADC_DRY  4095
#define ADC_WET  1534
Revert main/CMakeLists.txt back to target main.c:

CMake
idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")
Flash the production logger target:

Bash
idf.py -p /dev/cu.usbmodem101 build flash

---



### deploy instructions
1. Setting Up "Sensor Mode" (Normal Logging Cycle)When you power on or reset the ESP32-C3 without holding any buttons, it automatically defaults to Sensor Logging Mode.What to expect during a clean boot:Plug in your ESP32-C3 SuperMini via USB-C (ensure you are not holding the BOOT button).Open your serial monitor in VS Code or Terminal:Bashidf.py -p /dev/cu.usbmodem101 monitor
The 3-Second Window: You will see this line in the console:Booting... 3 seconds to hold BOOT button for Wired CSV Download Mode.Do nothing! Let those 3 seconds elapse without touching any buttons.Execution: The chip will automatically transition into normal sensor logging:It recovers its hourly timeline from SPIFFS flash memory.It energizes GPIO 10 for 800 ms to power the sensor probe.It reads GPIO 0 (ADC1_CH0), taking 16 averaged samples.It turns off power to GPIO 10.It buffers the reading in RAM (buffer_index++).It prints: Logged Hour X | Moisture: Y | Buffer: 1/24.It sets GPIO 2 as a low-power interrupt wakeup and goes into Deep Sleep for 1 hour (3600s).💡 Bench Testing Tip: Since nothing is wired to GPIO 0 yet, the ADC pin is floating. Your printed moisture value will read near 0 or fluctuate wildly—this is expected until a sensor is attached!2. Reading Logged Data (Wired Mode)To extract your logged CSV data over the USB cable without needing Bluetooth or the reed switch, you trigger the Software Download Window.1.Start the Serial Monitor:Open serial output.Launch the monitor in your VS Code terminal:Bashidf.py -p /dev/cu.usbmodem101 monitor

2.Tap the Physical RESET Button:Reboot the software.
Press and release the RESET button on the ESP32-C3 SuperMini board. (Do NOT touch the BOOT button yet!)

3.Press and Hold the BOOT Button:Intercept during the 3-second window.As soon as text starts scrolling in the serial monitor, press and hold down the BOOT button (GPIO 9).

4.Wait for CSV Confirmation:Verify payload and release.Keep holding the BOOT button until you see this output in your terminal:Plaintext*** DOWNLOAD MODE DETECTED ***
Starting CSV Export...

--- START CSV ---
Hour_Timestamp,Raw_ADC
1,124
2,118
--- END CSV ---

Data export complete. Board is now paused.
Once you see Data export complete, release the BOOT button. The ESP32 will stay safely awake on your bench without going back to deep sleep, keeping your USB serial connection active.⚠️ Common Bench Trap to AvoidDo NOT hold the BOOT button before or while tapping RESET. Doing so triggers the ESP32-C3 hardware ROM Bootloader (boot:0x7), causing macOS to throw Device not configured (Errno 6).Always let the chip start booting normally, then press BOOT during the 3-second grace period!


## ⏰ Time Synchronization & Deployment Procedure

Since the node operates completely offline without Wi-Fi, GPS, or an onboard RTC battery, time is tracked internally as relative runtime hours (`Hour 1`, `Hour 2`, `Hour 3`...). 

To accurately map logged hour numbers to real-world calendar timestamps, follow this zeroing protocol during field installation.

---

### 1. The Time Sync Strategy
When you deploy the node, you anchor `Hour 1` to a specific real-world timestamp recorded on your phone or field clipboard:

$$\text{Real World Timestamp} = \text{Deployment Timestamp} + ((\text{Logged Hour} - 1) \times 1\text{ Hour})$$

For example, if you record the deployment timestamp on your phone as **June 10 at 09:00 AM**, then:
* `Hour 1` in the log = **June 10, 09:00 AM**
* `Hour 24` in the log = **June 11, 08:00 AM**
* `Hour 168` in the log = **June 17, 08:00 AM**

---

### 2. Field Deployment & Memory Zeroing Checklist

Follow this exact sequence when installing a node at a new cacao tree:

<Sequence>
{/* Reason: Procedural installation steps where exact sequence is required to erase previous test logs and establish an accurate baseline hour in field records. */}
  <Step subtitle="Wipe bench testing artifacts" title="1. Clear Legacy Flash Memory">
    Before closing the enclosure, connect the node to your laptop or hold the BOOT button to ensure all previous bench test logs are cleared or re-formatted if setting up a fresh SPIFFS partition.
  </Step>
  <Step subtitle="Prepare the field record" title="2. Position Probe & Seal Box">
    Bore the soil hole, insert the probe prongs firmly into undisturbed soil at the target depth, close the case, and seal the cable gland.
  </Step>
  <Step subtitle="Establish time anchor" title="3. Execute Magnet Swipe Start">
    Swipe a magnet over the **NO Reed Switch** marker on the box. 
    
    * This wakes the board, starts the 60-second BLE window (`IEx_Tree_1`), and initializes/recovers the timeline.
    * **Immediately log the current time on your phone** (e.g., *Tree #4 deployed on Oct 12 @ 14:15*).
  </Step>
  <Step subtitle="Confirm setup in app" title="4. Verify Initial Readiness via BLE">
    Open **nRF Connect** on your mobile phone, connect to `IEx_Tree_1`, and read Characteristic `0xFFF1`. Confirm that:
    * `Hour` reads `0` or `1`.
    * `Last ADC` shows a valid reading corresponding to moist soil (e.g., between your wet/dry calibration bounds, not floating at 0 or 4095).
  </Step>
  <Step subtitle="Leave node to log" title="5. Allow Automatic Hourly Sleep">
    Walk away. After the 60-second BLE window expires, the node enters deep sleep and takes its first official hourly reading at $t + 1\text{ hour}$.
  </Step>
</Sequence>

---

### 3. Data Reconstruction Script (Post-Retrieval)

When you download the CSV data in the field via BLE or USB, the output looks like this:

```csv
Hour_Timestamp,Raw_ADC
1,2150
2,2142
3,2180
```

To convert this into standard ISO dates in Excel, Google Sheets, or Python:

Excel / Google Sheets Formula:
Assuming your Deployment Date/Time is in cell D1 (formatted as YYYY-MM-DD HH:MM) and your Hour_Timestamp starts in cell A2:

Excel
= $D$1 + ((A2 - 1) / 24)
Python Data Pandas Example:

Python
import pandas as pd

# Load retrieved CSV
df = pd.read_csv("cacao_log.csv")

# Define your recorded field deployment anchor
deployment_time = pd.Timestamp("2026-10-12 14:15:00")

# Calculate exact datetime for every hour
df["Datetime"] = deployment_time + pd.to_timedelta(
    df["Hour_Timestamp"] - 1, unit="h"
)
print(df[["Datetime", "Raw_ADC"]])


## 🧹 How to Wipe Logged Data (Reset for Testing)

During bench testing or prior to deploying at a new cacao tree, you must erase legacy log records from internal SPIFFS memory. This resets `total_hours_run` back to `0` and clears `cacao_log.bin`.

Choose one of the two methods below to clear the node's storage:

---

### Option A: Flash Memory Format Script (Recommended)

To completely re-format the `/data` SPIFFS partition and wipe all stored binaries:

<Sequence>
{/* Reason: Sequential instructions for flashing a dedicated wiper routine to format SPIFFS flash memory. */}
  <Step subtitle="Enable format on mount" title="1. Add Quick Wipe Call in main.c">
    Temporarily add `esp_spiffs_format("storage");` at the top of `app_main()` in your firmware:

    ```c
    void app_main(void) {
        // --- TEMPORARY WIPE ROUTINE ---
        ESP_LOGW(TAG, "Formatting SPIFFS storage partition...");
        esp_spiffs_format("storage");
        total_hours_run = 0;
        buffer_index = 0;
        ESP_LOGI(TAG, "Storage wiped successfully!");
        // ------------------------------
        
        // ... rest of app_main()
    }
    ```
  </Step>
  <Step subtitle="Upload format routine" title="2. Flash to ESP32-C3">
    Compile and upload the firmware to your board:
    ```bash
    idf.py -p /dev/cu.usbmodem101 flash monitor
    ```
  </Step>
  <Step subtitle="Prevent continuous wiping" title="3. Revert main.c & Re-flash">
    Once you see `Storage wiped successfully!` in the serial log, **comment out or delete** the `esp_spiffs_format("storage");` line, then flash your normal logging firmware back onto the board.
  </Step>
</Sequence>

---

### Option B: Terminal Command Line Erase (Full Chip Wipe)

If you want to erase all SPIFFS files, non-volatile memory (NVS), and cached variables in one step directly from your Mac terminal without modifying `main.c`:

<Sequence>
{/* Reason: Command-line sequence using esptool to wipe flash memory. */}
  <Step subtitle="Completely erases internal flash" title="1. Run esptool Erase Flash">
    In your VS Code terminal, execute:
    ```bash
    idf.py -p /dev/cu.usbmodem101 erase-flash
    ```
    *This wipes the partition table, NVS key-value storage, and SPIFFS binary logs.*
  </Step>
  <Step subtitle="Re-deploy fresh partition table and firmware" title="2. Re-flash the Project">
    Re-upload your project firmware:
    ```bash
    idf.py -p /dev/cu.usbmodem101 flash
    ```
    *Upon first boot, `save_buffer_to_flash()` will automatically re-create a clean, empty `/data/cacao_log.bin` file.*
  </Step>
</Sequence>

---

### 🔍 How to Verify Memory is Clean

After executing either wipe method, test the board's memory status:

1. Connect the node via USB and open `idf.py monitor`.
2. Tap `RESET` and hold `BOOT` during the 3-second startup window to enter **Wired CSV Download Mode**.
3. Confirm that the serial output confirms an empty file:
   ```text
   *** DOWNLOAD MODE DETECTED ***
   Starting CSV Export...

   --- START CSV ---
   Hour_Timestamp,Raw_ADC
   --- END CSV ---

   Data export complete. Board is now paused.

# power consumption old
3v 
0.008 W Sleep
0.135 W awake 
(Without sensor connected)

# BLE server 

Can be used to read latest data, but not retrieving all the data as it may cost a lot of power.

# theorectical new with esp32-3C
and CR123A panasonic (about 1.4k mAh)
Usable Capacity: 1,400 mAh * 0.80 = 1,120 mAh

Average Hourly Draw: 0.122 mA (using the C3 SuperMini waking up once an hour to read two sensors and write to flash)

The Calculation:

1,120 mAh / 0.122 mA = 9,180 Hours

9,180 Hours / 24 = 382 Days

Actual mA draw:
15 mA when active, 0.3 mA when deep sleep. 
1,120 mAh / 0.3 mA / 24 = 155 days

# Solar panel pre-setup
Treat the 5V mini solar panels with a ceramic car coating or a high-quality glass water repellent (like Rain-X).

Clean the mini solar panels perfectly with rubbing alcohol.

Apply the hydrophobic coating and buff it out.

Mount the panels at a slight angle (at least 15 degrees) on the sensor casing
