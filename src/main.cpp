#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <DHT.h>

// ESP-NOW receiver MAC address (broadcast to all devices)
// For specific device, update this or use config.h
uint8_t receiverAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Sensor pins
#define DHTPIN 1  // D1 on XIAO ESP32C6
#define DHTTYPE DHT22

#define LED_PIN 15

// Initialize sensors
Adafruit_BMP085 bmp;
DHT dht(DHTPIN, DHTTYPE);

// Data structure for ESP-NOW
typedef struct struct_message {
  float temperature;
  float humidity;
  float pressure;
  float battery;
} struct_message;

// Define sleep time in seconds and conversion factor
#define TIME_TO_SLEEP 120 // Sleep for 120 seconds
#define uS_TO_S_FACTOR 1000000 // Conversion factor for microseconds to seconds

// Set to false to disable sleep for testing
#define ENABLE_SLEEP true

// Forward declarations
void sendData();
void blinkLED();

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

  // Initialize BMP180
  if (!bmp.begin()) {
    Serial.println("BMP180 init failed!");
    while(1);
  }
  Serial.println("BMP180 initialized");

  dht.begin();
  Serial.println("DHT22 initialized");

  // Give DHT22 time to stabilize after power-on
  Serial.println("Waiting 2s for DHT22 to stabilize...");
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

  // Read sensor data
  Serial.println("Reading DHT22...");
  float dht_temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  // Check if DHT22 readings are valid
  if (isnan(dht_temperature) || isnan(humidity)) {
    Serial.println("DHT22 reading FAILED - invalid data");
    dht_temperature = -999.0;
    humidity = -999.0;
  } else {
    Serial.print("Temperature: ");
    Serial.print(dht_temperature);
    Serial.println(" °C");
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
  }

  // Read BMP180 pressure sensor
  Serial.println("Reading BMP180...");
  float bmp_temperature = bmp.readTemperature();
  float pressure = bmp.readPressure() / 100.0F; // Convert Pa to hPa
  Serial.print("BMP180 Temperature: ");
  Serial.print(bmp_temperature);
  Serial.println(" °C");
  Serial.print("Pressure: ");
  Serial.print(pressure);
  Serial.println(" hPa");

  // Prepare CSV data string
  char csvData[100];
  snprintf(csvData, sizeof(csvData), "%.2f,%.2f,%.2f,%.2f",
           dht_temperature, humidity, pressure, Vbattf);

  // Send message via ESP-NOW
  Serial.println("\nSending data via ESP-NOW...");
  Serial.print("CSV Data: ");
  Serial.println(csvData);

  esp_err_t result = esp_now_send(receiverAddress, (uint8_t *)csvData, strlen(csvData));

  if (result == ESP_OK) {
    Serial.println("Data sent successfully!");
    blinkLED();
  } else {
    Serial.print("Send failed with error: ");
    Serial.println(result);
  }
}

void blinkLED() {
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW);
}

