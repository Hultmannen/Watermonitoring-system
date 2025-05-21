#include <SPI.h>
#include <SD.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// === PIN DEFINITIONS ===
#define SD_CS     9      // SD card Chip Select
#define TDS_PIN   6      // TDS sensor analog pin (change to 34 if needed)
#define ONE_WIRE_BUS 4   // DS18B20 data pin
#define TURBIDITY_PIN 8
#define PH_PIN 5

// === OBJECTS ===
SPIClass hspi(HSPI);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// === STATE VARIABLES ===
bool loggingActive = false;
bool fileSelected = false;
unsigned long lastLogTime = 0;
const unsigned long logInterval = 2000;
String filename = "";
float currentTemperature = 25.0;  // Default temperature if sensor fails

// === SETUP ===
void setup() {
  Serial.begin(115200);
  analogReadResolution(12); // 12-bit ADC

  // Initialize SPI for SD card
  hspi.begin(11, 12, 10, SD_CS);

  // Initialize SD card
  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS, hspi)) {
    Serial.println("SD card initialization failed!");
    return;
  }
  Serial.println("✅ SD card initialized.");

  // Initialize temperature sensor
  sensors.begin();

  Serial.println("📌 Use 'file yourname' to create/select a CSV.");
  Serial.println("Then use 'start' to begin logging, 'stop' to pause.");
}

// === LOOP ===
void loop() {
  handleSerialCommands();

  if (loggingActive && fileSelected && millis() - lastLogTime >= logInterval) {
    sensors.requestTemperatures();
    currentTemperature = sensors.getTempCByIndex(0);
    logTDSReading();
    lastLogTime = millis();
  }
}

// === HANDLE SERIAL COMMANDS ===
void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.equalsIgnoreCase("start")) {
      if (!fileSelected) {
        Serial.println("⚠️ No file selected. Use 'file yourname' first.");
        return;
      }
      loggingActive = true;
      Serial.printf("✅ Logging started to %s\n", filename.c_str());
    } else if (command.equalsIgnoreCase("stop")) {
      loggingActive = false;
      Serial.println("🛑 Logging stopped.");
    } else if (command.startsWith("file ")) {
      String newName = command.substring(5);
      newName.trim();
      if (!newName.endsWith(".csv")) {
        newName += ".csv";
      }

      filename = "/" + newName;
      fileSelected = true;
      Serial.printf("📂 File set to: %s\n", filename.c_str());

      ensureFileExistsWithHeader();
    } else {
      Serial.println("❓ Unknown command. Use 'start', 'stop', or 'file filename'");
    }
  }
}

// === CREATE FILE HEADER IF NEW ===
void ensureFileExistsWithHeader() {
  if (!SD.exists(filename)) {
    File file = SD.open(filename, FILE_WRITE);
    if (file) {
      file.println("Seconds;TDS (ppm);Temperature (°C);Turbidity (NTU);pH\r\n");
      file.close();
      Serial.printf("📝 Created new file with header: %s\n", filename.c_str());
    } else {
      Serial.println("❌ Failed to create file.");
    }
  }
}

// === LOG TDS & TEMPERATURE TO CSV ===
void logTDSReading() {
  int raw = analogRead(TDS_PIN);
  float voltage = raw * (3.3 / 4095.0);

  // Uncompensated TDS calculation
  float tds = (133.42 * pow(voltage, 3)) -
              (255.86 * pow(voltage, 2)) +
              (857.39 * voltage);

  // Temperature compensation
  float compensationCoefficient = 1.0 + 0.02 * (currentTemperature - 25.0);
  float compensatedTds = tds / compensationCoefficient;

  unsigned long now = millis() / 1000;

  float calibrationFactor = 0.45;  // Adjust this value as needed
  float calibratedTDS = compensatedTds * calibrationFactor;
  
  // === pH Reading ===
  int phRaw = analogRead(PH_PIN);
  float phVoltage = phRaw * (3.3 / 4095.0);
  float pH = 3.5 * phVoltage + 0.55;  // Adjust offset after calibration

  // === Turbidity Reading ===
  int turbidityRaw = analogRead(TURBIDITY_PIN);
  float turbidityVoltage = turbidityRaw * (3.3 / 4095.0);

  // Cleaner range for lakes, rivers, and tap
  float turbidityNTU = (3.3 - turbidityVoltage) * 100;  // Scale: 0–100 NTU

  // Clamp value to 0 (no stnegative turbidity)
  if (turbidityNTU > 3000) turbidityNTU = 3000;  // Max NTU clamp
  Serial.printf("📟 Turbidity voltage: %.3f V\n", turbidityVoltage);



  File file = SD.open(filename, FILE_APPEND);
  if (file) {
    file.printf("%lu;%.2f;%.2f;%.2f;%.2f\r\n", now, calibratedTDS, currentTemperature, turbidityNTU, pH);
    file.close();
    Serial.printf("📈 Logged: %lu s | TDS: %.2f ppm | Temp: %.2f °C | Turb: %.2f NTU | pH: %.2f\n",
              now, calibratedTDS, currentTemperature, turbidityNTU, pH);
    Serial.printf("📟 pH Voltage: %.3f V | pH: %.2f\n", phVoltage, pH);
  } else {
    Serial.println("❌ Failed to write to file.");
  }
}

