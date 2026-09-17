#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

// ESP-NOW receiver MAC address
// Default to broadcast address - create config.h to override with specific MAC
#ifdef INCLUDE_CONFIG
  #include "config.h"
#else
  uint8_t receiverAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#endif

#define LED_PIN 2

// Must match ESPNOW_CHANNEL in the esp-now-listener receiver
#define ESPNOW_CHANNEL 1

// Initialize AHT20 (temperature/humidity) and BMP280 (pressure) sensors
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
bool ahtAvailable = true;
bool bmpAvailable = true;

// Data structure for ESP-NOW
typedef struct __attribute__((packed)) struct_message {
  float temperature;
  float humidity;
  float pressure;
  float gas;  // Always NaN - AHT20/BMP280 has no gas sensing (kept for wire compatibility with the BME680 relay/MQTT/Pi consumers)
  float battery;
  uint32_t checksum;  // Simple checksum for data integrity
} struct_message;

// Station elevation, used to adjust absolute pressure to sea-level-equivalent
#define STATION_ALTITUDE_METERS 209.7 // 688 ft

// Define sleep time in seconds and conversion factor
#define TIME_TO_SLEEP 120
#define uS_TO_S_FACTOR 1000000 // Conversion factor for microseconds to seconds

// Set to false to disable sleep for testing
#define ENABLE_SLEEP true

// Callback status variables
volatile bool messageSent = false;
volatile bool sendSuccess = false;

// Forward declarations
void sendData();
void blinkLED();
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
uint32_t calculateChecksum(struct_message* data);

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== ESP32 Weather Station ===");

  pinMode(LED_PIN, OUTPUT);

  // TEMP DEBUG: scan I2C bus to find connected devices
  Wire.begin();
  Serial.println("Scanning I2C bus...");
  int devicesFound = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  Found device at 0x");
      Serial.println(addr, HEX);
      devicesFound++;
    }
  }
  if (devicesFound == 0) {
    Serial.println("  No I2C devices found!");
  }

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Disable modem sleep so we don't miss the ESP-NOW ack window
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.println("WiFi mode set to STA");

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    return;
  }
  Serial.println("ESP-NOW initialized");

  // Register send callback
  esp_now_register_send_cb(onDataSent);
  Serial.println("Send callback registered");

  // Register peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer!");
    return;
  }
  Serial.println("Peer added successfully");

  // Initialize AHT20
  ahtAvailable = aht.begin();
  if (!ahtAvailable) {
    Serial.println("AHT20 init failed! Check wiring. Will still report other readings.");
  } else {
    Serial.println("AHT20 initialized");
  }

  // Initialize BMP280
  bmpAvailable = bmp.begin();
  if (!bmpAvailable) {
    Serial.println("BMP280 init failed! Check wiring. Will still report other readings.");
  } else {
    Serial.println("BMP280 initialized");

    // Forced mode takes a single on-demand reading, matching the BME680's single-shot behavior
    bmp.setSampling(Adafruit_BMP280::MODE_FORCED,
                     Adafruit_BMP280::SAMPLING_X1,  // Temperature oversampling (unused - we read temp from the AHT20)
                     Adafruit_BMP280::SAMPLING_X16, // Pressure oversampling
                     Adafruit_BMP280::FILTER_OFF,
                     Adafruit_BMP280::STANDBY_MS_1);
  }

  // Read and send data before going to sleep
  sendData();

#if ENABLE_SLEEP
  // Set the timer to wake up after TIME_TO_SLEEP seconds
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  Serial.print("\nEntering deep sleep for ");
  Serial.print(TIME_TO_SLEEP);
  Serial.println(" seconds...");
  Serial.flush(); // Ensure all serial data is sent before sleep
  delay(100);

  // Start deep sleep
  esp_deep_sleep_start();
#else
  Serial.println("\nSleep disabled - running in continuous mode");
#endif
}

void loop() {
#if !ENABLE_SLEEP
  // Continuous operation mode for testing
  delay(TIME_TO_SLEEP * 1000);
  sendData();
#else
  // This will not be called because the ESP32 goes to sleep after setup.
#endif
}

void sendData() {
  Serial.println("\n--- Reading Sensors ---");

  //Read battery
  uint32_t Vbatt = 0;
  for (int i = 0; i < 16; i++) {
    Vbatt += analogReadMilliVolts(A0); // Read ADC with correction
  }
  float Vbattf = 5 * Vbatt / 16 / 1000.0; // Adjust for divider ratio (30k/7.5k = 5:1)
  Serial.print("Battery Voltage: ");
  Serial.print(Vbattf);
  Serial.println(" V");

  // Read AHT20/BMP280 sensors (NaN sentinel values mean sensor missing/unreadable this cycle)
  float temperature = NAN;
  float humidity = NAN;
  float pressure = NAN;
  float gas = NAN; // No gas sensing on AHT20/BMP280 - always NaN

  if (!ahtAvailable) {
    Serial.println("AHT20 not available - skipping temperature/humidity");
  } else {
    Serial.println("Reading AHT20...");
    sensors_event_t humidityEvent, tempEvent;
    if (!aht.getEvent(&humidityEvent, &tempEvent)) {
      Serial.println("AHT20 reading FAILED!");
    } else {
      temperature = tempEvent.temperature;
      humidity = humidityEvent.relative_humidity;

      Serial.print("Temperature: ");
      Serial.print(temperature);
      Serial.println(" °C");
      Serial.print("Humidity: ");
      Serial.print(humidity);
      Serial.println(" %");
    }
  }

  if (!bmpAvailable) {
    Serial.println("BMP280 not available - skipping pressure");
  } else {
    Serial.println("Reading BMP280...");
    if (!bmp.takeForcedMeasurement()) {
      Serial.println("BMP280 reading FAILED!");
    } else {
      pressure = bmp.readPressure() / 100.0; // Convert Pa to hPa
      // Adjust station (absolute) pressure to sea-level-equivalent using the standard barometric formula
      pressure = pressure / pow(1.0 - (STATION_ALTITUDE_METERS / 44330.0), 5.255);

      Serial.print("Pressure (sea level): ");
      Serial.print(pressure);
      Serial.println(" hPa");
    }
  }

  // Prepare binary struct data
  struct_message sensorData;
  sensorData.temperature = temperature;
  sensorData.humidity = humidity;
  sensorData.pressure = pressure;
  sensorData.gas = gas;
  sensorData.battery = Vbattf;
  sensorData.checksum = calculateChecksum(&sensorData);

  // Send message via ESP-NOW with retry mechanism
  Serial.println("\nSending data via ESP-NOW...");
  Serial.print("Struct Data (");
  Serial.print(sizeof(sensorData));
  Serial.print(" bytes): temp=");
  Serial.print(sensorData.temperature);
  Serial.print("°C, hum=");
  Serial.print(sensorData.humidity);
  Serial.print("%, press=");
  Serial.print(sensorData.pressure);
  Serial.print("hPa, gas=");
  Serial.print(sensorData.gas);
  Serial.print("KΩ, batt=");
  Serial.print(sensorData.battery);
  Serial.print("V, checksum=0x");
  Serial.println(sensorData.checksum, HEX);

  const int MAX_RETRIES = 3;
  bool delivered = false;

  for (int attempt = 1; attempt <= MAX_RETRIES && !delivered; attempt++) {
    Serial.print("Attempt ");
    Serial.print(attempt);
    Serial.print(" of ");
    Serial.println(MAX_RETRIES);

    // Reset callback flags
    messageSent = false;
    sendSuccess = false;

    // Send the message
    esp_err_t result = esp_now_send(receiverAddress, (uint8_t *)&sensorData, sizeof(sensorData));

    if (result == ESP_OK) {
      Serial.println("Message queued successfully");

      // Wait for callback confirmation (timeout after 1 second)
      unsigned long startTime = millis();
      while (!messageSent && (millis() - startTime < 1000)) {
        delay(10);  // Small delay to allow callback to execute
      }

      if (messageSent) {
        if (sendSuccess) {
          Serial.println("✓ Radio transmit confirmed (receiver ack received)");
          delivered = true;
          blinkLED();
        } else {
          Serial.println("✗ Radio transmit failed");
        }
      } else {
        Serial.println("✗ Callback timeout - no response from receiver");
      }
    } else {
      Serial.print("✗ Failed to queue message, error: ");
      Serial.println(result);
    }

    // If not delivered and more attempts remain, wait before retry
    if (!delivered && attempt < MAX_RETRIES) {
      Serial.println("Retrying in 100ms...");
      delay(100);
    }
  }

  if (!delivered) {
    Serial.println("!!! WARNING: Failed to deliver message after all retries !!!");
  }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // This callback is called when ESP-NOW receives acknowledgment from receiver
  messageSent = true;
  sendSuccess = (status == ESP_NOW_SEND_SUCCESS);

  // Optional: Print MAC address for debugging
  // Serial.printf("Packet to %02X:%02X:%02X:%02X:%02X:%02X ",
  //               mac_addr[0], mac_addr[1], mac_addr[2],
  //               mac_addr[3], mac_addr[4], mac_addr[5]);
  // Serial.println(sendSuccess ? "delivered" : "failed");
}

void blinkLED() {
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW);
}

uint32_t calculateChecksum(struct_message* data) {
  // Simple checksum: XOR all bytes of the float values
  uint32_t checksum = 0;
  uint8_t* ptr = (uint8_t*)data;

  // Calculate over all fields except the checksum field itself
  size_t dataSize = sizeof(struct_message) - sizeof(uint32_t);

  for (size_t i = 0; i < dataSize; i++) {
    checksum ^= ptr[i];
    checksum = (checksum << 1) | (checksum >> 31);  // Rotate left
  }

  return checksum;
}

