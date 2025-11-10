This is my home weather station built with a Seeed studio ESP32-C6

It measures temperature and humidity with a DHT22 and pressure with a BMP180.

Then it transmits the data over ESPNOW. I have an ESPNOW to Wifi bridge inside the house that sends the data to a MQTT on my Kubernetes cluster.

The details of the ESPNOW to Wifi bridge is here: https://github.com/Skyfarer/esp-now2wifi2mqtt

## Photos

### The Weather Station
![Weather station installed outside](images/outside.jpeg)

### Stevenson Screen
![Stevenson screen housing](images/stevenson_screen.jpeg)

### Solar Panel
![Solar panel for power](images/solar-panel.jpeg)

### Hardware Components
![ESP32-C6 board with sensors](images/board.jpeg)

### External Antenna
![External antenna](images/antenna.jpeg)

## Technical Details

### Hardware
- **Board**: Seeed XIAO ESP32-C6
- **Sensors**:
  - DHT22 on GPIO1 (D1) - Temperature and humidity
  - BMP180 via I2C - Barometric pressure
- **Battery monitoring**: ADC on A0 with 2:1 voltage divider
- **Status LED**: GPIO15

### Power Management
- Deep sleep mode enabled to conserve battery
- Wake interval: 60 seconds (currently set to 5 seconds for testing, configurable via `TIME_TO_SLEEP`)
- Sensor reading and data transmission occur during brief wake periods
- Current implementation reads sensors → transmits → sleeps

### Data Format
Data is transmitted via ESP-NOW as comma-separated values (CSV):
```
temperature,humidity,pressure,battery
```
Example: `23.50,65.20,1013.25,3.85`

### Build and Upload
```bash
pio run --target upload --target monitor
```

### Configuration
Create a `config.h` file in the root directory for your receiver MAC address:
```cpp
uint8_t receiverAddress[] = {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX};
```
By default, the code uses broadcast address (0xFF:FF:FF:FF:FF:FF).
