# Install esptool
pip install esptool

# Flash using the generated arguments
esptool.py --chip esp32 -p /dev/ttyUSB0 write_flash @flasher_args.json

or 
idf.py -p /dev/cu.usbserial-0001

idf.py -p /dev/cu.usbserial-0001 -b 115200 monitor

idf.py -p /dev/cu.usbserial-0001 -b 115200 flash monitor

to see monitor

ls /dev/cu.*
to list whihc port (macos)

# to read flash

esptool.py -p /dev/cu.usbserial-0001 -b 460800 read_flash 0x110000 0x200000 cacao_data_raw.bin

# to clear flash
idf.py erase-flash

# to convert bin to csv

hardware tested on:
Chipset: ESP32-WROOM-32

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

> **⚠️ Note on ESP32 Hardware Revisions:**
> This deployment utilizes a mix of 30-pin and 32-pin (USB-C) ESP32 development boards. The compiled firmware (`.bin`) is 100% compatible across both versions. However, the physical locations of GPIO 25 and GPIO 34 differ significantly between board shapes. **Always wire according to the silkscreen labels (e.g., "G25", "G34") printed on the PCB**, rather than counting pins from the edge.

### Design Notes
* **Why ADC1 (GPIO 34)?** The ESP32 features two Analog-to-Digital Converters. ADC2 is shared with the Wi-Fi radio and will fail to read if Wi-Fi is active. By forcing the hardware design to use `ADC1_CH6` (GPIO 34), we guarantee that future firmware upgrades involving wireless transmission will not conflict with soil data collection.
* **Input Only:** GPIO 34, 35, 36, and 39 are "Input-Only" pins on the ESP32. They do not have internal pull-up or pull-down resistors. This makes GPIO 34 ideal for pure analog readings without internal hardware skewing the voltage.

## to read mac
esptool.py -p /dev/cu.usbserial-0001 read_mac

# power consumption
3v 
0.008 W Sleep
0.135 W awake 
(Without sensor connected)

# Solar panel pre-setup
Treat the 5V mini solar panels with a ceramic car coating or a high-quality glass water repellent (like Rain-X).

Clean the mini solar panels perfectly with rubbing alcohol.

Apply the hydrophobic coating and buff it out.

Mount the panels at a slight angle (at least 15 degrees) on the sensor casing
