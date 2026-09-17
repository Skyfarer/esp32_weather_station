This is my home weather station built with an ESP32-D board

It measures temperature and humidity with an AHT20, and pressure with a BMP280 ~~and gas resistance (air quality) with a BME680~~ ~~DHT22 and pressure with a BMP180~~. I swapped the BME680 for an AHT20+BMP280 combo module, trading away air quality sensing.

It transmits the data over ESP-NOW to an ESP32-C6 inside the house, which relays it to MQTT. A Raspberry Pi Zero subscribes to MQTT and drives an e-ink display.

## Photos

### The Weather Station
![Weather station installed outside](images/outside.jpeg)

### Stevenson Screen
![Stevenson screen housing](images/stevenson_screen.jpeg)

The Stevenson screen housing is 3D printed using this design: https://www.thingiverse.com/thing:2970799

### Solar Panel
![Solar panel for power](images/solar-panel.jpeg)

### Hardware Components
![ESP32-C6 board with sensors](images/board.jpeg)

### External Antenna
![External antenna](images/antenna.jpeg)

## Technical Details

### Hardware
- **Board**: Generic ESP32-D (ESP32-D0WD)
- **Sensors**:
  - AHT20 via I2C - Temperature and humidity
  - BMP280 via I2C - Barometric pressure
  - ~~BME680 via I2C - Temperature, Humidity, Pressure, and Gas Resistance (air quality) sensor breakout board~~
  - ~~DHT22 on GPIO1 (D1) - Temperature and humidity~~
  - ~~BMP180 via I2C - Barometric pressure~~
- **Battery monitoring**: ADC on A0 with 2:1 voltage divider
- **Status LED**: GPIO2 (onboard)

### Power Management
- Deep sleep mode enabled to conserve battery
- Wake interval: 60 seconds (currently set to 5 seconds for testing, configurable via `TIME_TO_SLEEP`)
- Sensor reading and data transmission occur during brief wake periods
- Current implementation reads sensors → transmits → sleeps

### Data Format
Data is transmitted via ESP-NOW as comma-separated values (CSV):
```
temperature,humidity,pressure,gas,battery
```
- Temperature in °C
- Humidity in %
- Pressure in hPa
- Gas resistance in KOhms (air quality indicator) - always NaN since the AHT20/BMP280 swap; kept in the struct for wire compatibility with the relay/MQTT/Pi
- Battery voltage in V

Example: `23.50,65.20,1013.25,45.32,3.85`

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
