This node is an ultra-low-power, offline soil logging system built around the ESP32-C3 SuperMini (RISC-V). It operates on 2x AA Li-FeS2 batteries wired directly to the 3.3V power rail, using deep-sleep state management and internal SPIFFS storage to maximize battery longevity.

# Install esptool
pip install esptool

# Flash using the generated arguments
esptool.py --chip esp32 -p /dev/ttyUSB0 write_flash @flasher_args.json

or 
idf.py -p /dev/cu.usbserial-0001

idf.py -p /dev/cu.usbserial-0001 -b 115200 monitor

idf.py -p /dev/cu.usbserial-0001 -b 115200 flash monitor


idf.py -p /dev/cu.usbmodem101 -b 115200 flash monitor

to see monitor

ls /dev/cu.*
to list whihc port (macos)

# to read flash

esptool.py -p /dev/cu.usbserial-0001 -b 460800 read_flash 0x110000 0x200000 cacao_data_raw.bin

# to clear flash
idf.py erase-flash

# to convert bin to csv

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


## instructions
1. Setting Up "Sensor Mode" (Normal Logging Cycle)When you power on or reset the ESP32-C3 without holding any buttons, it automatically defaults to Sensor Logging Mode.What to expect during a clean boot:Plug in your ESP32-C3 SuperMini via USB-C (ensure you are not holding the BOOT button).Open your serial monitor in VS Code or Terminal:Bashidf.py -p /dev/cu.usbmodem101 monitor
The 3-Second Window: You will see this line in the console:Booting... 3 seconds to hold BOOT button for Wired CSV Download Mode.Do nothing! Let those 3 seconds elapse without touching any buttons.Execution: The chip will automatically transition into normal sensor logging:It recovers its hourly timeline from SPIFFS flash memory.It energizes GPIO 10 for 800 ms to power the sensor probe.It reads GPIO 0 (ADC1_CH0), taking 16 averaged samples.It turns off power to GPIO 10.It buffers the reading in RAM (buffer_index++).It prints: Logged Hour X | Moisture: Y | Buffer: 1/24.It sets GPIO 2 as a low-power interrupt wakeup and goes into Deep Sleep for 1 hour (3600s).💡 Bench Testing Tip: Since nothing is wired to GPIO 0 yet, the ADC pin is floating. Your printed moisture value will read near 0 or fluctuate wildly—this is expected until a sensor is attached!2. Reading Logged Data (Wired Mode)To extract your logged CSV data over the USB cable without needing Bluetooth or the reed switch, you trigger the Software Download Window.1.Start the Serial Monitor:Open serial output.Launch the monitor in your VS Code terminal:Bashidf.py -p /dev/cu.usbmodem101 monitor
2.Tap the Physical RESET Button:Reboot the software.Press and release the RESET button on the ESP32-C3 SuperMini board. (Do NOT touch the BOOT button yet!)3.Press and Hold the BOOT Button:Intercept during the 3-second window.As soon as text starts scrolling in the serial monitor, press and hold down the BOOT button (GPIO 9).4.Wait for CSV Confirmation:Verify payload and release.Keep holding the BOOT button until you see this output in your terminal:Plaintext*** DOWNLOAD MODE DETECTED ***
Starting CSV Export...

--- START CSV ---
Hour_Timestamp,Raw_ADC
1,124
2,118
--- END CSV ---

Data export complete. Board is now paused.
Once you see Data export complete, release the BOOT button. The ESP32 will stay safely awake on your bench without going back to deep sleep, keeping your USB serial connection active.⚠️ Common Bench Trap to AvoidDo NOT hold the BOOT button before or while tapping RESET. Doing so triggers the ESP32-C3 hardware ROM Bootloader (boot:0x7), causing macOS to throw Device not configured (Errno 6).Always let the chip start booting normally, then press BOOT during the 3-second grace period!

## how to get data (old)
How to execute the test:

Flash this updated code (you may have to hold BOOT while plugging it in one last time to get the flash to work).

Once the code is flashed, open your serial monitor.

Press the RESET button on the board.

Immediately press and hold the BOOT button down.

Watch the serial monitor. Wait until you see *** DOWNLOAD MODE DETECTED *** and the CSV printout before you let go of the button.

# power consumption
3v 
0.008 W Sleep
0.135 W awake 
(Without sensor connected)

# theorectical new with esp32-3C
and CR123A panasonic (about 1.4k mAh)
Usable Capacity: 1,400 mAh * 0.80 = 1,120 mAh

Average Hourly Draw: 0.122 mA (using the C3 SuperMini waking up once an hour to read two sensors and write to flash)

The Calculation:

1,120 mAh / 0.122 mA = 9,180 Hours

9,180 Hours / 24 = 382 Days

# Solar panel pre-setup
Treat the 5V mini solar panels with a ceramic car coating or a high-quality glass water repellent (like Rain-X).

Clean the mini solar panels perfectly with rubbing alcohol.

Apply the hydrophobic coating and buff it out.

Mount the panels at a slight angle (at least 15 degrees) on the sensor casing
