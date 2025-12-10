#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"

// ESP-NOW receiver MAC address
// Default to broadcast address - create config.h to override with specific MAC
#ifdef INCLUDE_CONFIG
  #include "config.h"
#else
  uint8_t receiverAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#endif

#define LED_PIN 15

// Initialize BME680 sensor
Adafruit_BME680 bme;

// Data structure for ESP-NOW
typedef struct __attribute__((packed)) struct_message {
  float temperature;
  float humidity;
  float pressure;
  float gas;
  float battery;
  uint32_t checksum;  // Simple checksum for data integrity
} struct_message;

// Define sleep time in seconds and conversion factor
#define TIME_TO_SLEEP 60 // Sleep for 120 seconds
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

  // Switch to external antenna
  pinMode(WIFI_ENABLE, OUTPUT);
  digitalWrite(WIFI_ENABLE, LOW);
  delay(100);
  pinMode(WIFI_ANT_CONFIG, OUTPUT);
  digitalWrite(WIFI_ANT_CONFIG, HIGH);
  Serial.println("External antenna enabled");

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
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
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer!");
    return;
  }
  Serial.println("Peer added successfully");

  // Initialize BME680
  if (!bme.begin()) {
    Serial.println("BME680 init failed! Check wiring.");
    while(1);
  }
  Serial.println("BME680 initialized");

  // Set up BME680 oversampling and filter
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320°C for 150 ms

  Serial.println("Waiting 2s for BME680 to stabilize...");
  delay(2000);

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
  float Vbattf = 2 * Vbatt / 16 / 1000.0; // Adjust for divider ratio
  Serial.print("Battery Voltage: ");
  Serial.print(Vbattf);
  Serial.println(" V");

  // Read BME680 sensor
  Serial.println("Reading BME680...");
  if (!bme.performReading()) {
    Serial.println("BME680 reading FAILED!");
    return;
  }

  float temperature = bme.temperature;
  float humidity = bme.humidity;
  float pressure = bme.pressure / 100.0; // Convert Pa to hPa
  float gas = bme.gas_resistance / 1000.0; // Convert to KOhms

  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" °C");
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");
  Serial.print("Pressure: ");
  Serial.print(pressure);
  Serial.println(" hPa");
  Serial.print("Gas Resistance: ");
  Serial.print(gas);
  Serial.println(" KOhms");

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
          Serial.println("✓ Delivery confirmed by receiver!");
          delivered = true;
          blinkLED();
        } else {
          Serial.println("✗ Delivery failed - receiver did not acknowledge");
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

