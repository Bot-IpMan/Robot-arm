#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <EEPROM.h>

Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;

// Pin definitions
const int mq9_analog = A0;
const int temt6000_analog = A1;
const int LED_PIN = 13;  // Built-in LED for status indication

// System status flags
bool aht_ok = false;
bool bmp_ok = false;
bool system_ready = false;
bool usb_connected = false;

// Counters and timers
unsigned long loop_count = 0;
unsigned long last_sensor_check = 0;
unsigned long last_heartbeat = 0;
unsigned long last_reconnect_attempt = 0;
unsigned long boot_time = 0;

// Optimized timing for faster operation
const unsigned long SENSOR_CHECK_INTERVAL = 20000;  // 20 seconds
const unsigned long HEARTBEAT_INTERVAL = 5000;      // 5 seconds
const unsigned long RECONNECT_INTERVAL = 3000;      // 3 seconds
const unsigned long MAIN_LOOP_DELAY = 1000;         // 1 second main loop

// Watchdog variables
volatile bool watchdog_flag = false;
unsigned long last_watchdog_reset = 0;

// TEMT6000 calibration constants
const float TEMT6000_VOLTAGE_REF = 5.0;
const float TEMT6000_RESOLUTION = 1024.0;
const float TEMT6000_SENSITIVITY = 0.001;  // Approximate conversion factor

// Status structure for persistence
struct SystemStatus {
  bool first_boot;
  unsigned long total_runtime;
  unsigned int reset_count;
  unsigned int sensor_failures;
  unsigned long light_readings_count;
};

SystemStatus status;

// Data buffer for batch operations
struct SensorData {
  unsigned long timestamp;
  float temp_aht;
  float humidity;
  float temp_bmp;
  float pressure;
  int mq9_raw;
  int light_raw;
  float light_lux;
};

SensorData currentReading;

void setup() {
  boot_time = millis();
  
  // Initialize built-in LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // LED on during boot
  
  // Initialize serial and wait briefly for connection
  Serial.begin(115200);
  delay(1000);  // Give time for serial connection
  
  // Send initial debug message
  Serial.println(F("Starting Arduino Mega..."));
  Serial.flush();
  
  // Faster boot - reduced stabilization delay
  delay(2000);  // Reduced from 3000
  
  // Load system status from EEPROM
  loadSystemStatus();
  
  // Boot message
  Serial.println(F("=== Arduino Mega Environmental Monitor ==="));
  Serial.print(F("Boot time: "));
  Serial.println(boot_time);
  Serial.print(F("Reset count: "));
  Serial.println(status.reset_count);
  Serial.print(F("Free RAM: "));
  Serial.println(freeRam());
  
  // Initialize I2C with standard speed for stability
  Wire.begin();
  Wire.setClock(100000);  // Standard I2C speed for stability
  
  // Reduced sensor stabilization delay
  delay(1000);  // Reduced from 2000
  
  // Initialize sensors with faster retry
  initSensors();
  
  // Setup analog pins
  pinMode(mq9_analog, INPUT);
  pinMode(temt6000_analog, INPUT);
  
  // Configure ADC for faster readings (safer approach)
  // Keep default ADC settings for stability
  
  // Final setup messages
  Serial.println(F("=== Hardware Status ==="));
  Serial.print(F("AHT20: ")); Serial.println(aht_ok ? F("OK") : F("FAIL"));
  Serial.print(F("BMP280: ")); Serial.println(bmp_ok ? F("OK") : F("FAIL"));
  Serial.print(F("MQ-9: ")); Serial.println(F("OK (analog)"));
  Serial.print(F("TEMT6000: ")); Serial.println(F("OK (analog)"));
  
  // Update system status
  status.reset_count++;
  if (!aht_ok || !bmp_ok) {
    status.sensor_failures++;
  }
  saveSystemStatus();
  
  // System ready signal
  system_ready = true;
  usb_connected = true;
  
  // LED off - system ready
  digitalWrite(LED_PIN, LOW);
  
  // Critical handshake signal
  Serial.println(F("ARDUINO_READY"));
  Serial.flush();
  
  // Additional ready signals for reliability
  delay(50);  // Reduced delay
  Serial.println(F("ARDUINO_READY"));
  Serial.flush();
  
  Serial.println(F("=== System Ready - Starting Main Loop ==="));
  
  last_watchdog_reset = millis();
}

void initSensors() {
  Serial.println(F("=== Sensor Initialization ==="));
  Serial.flush();
  
  // AHT20 initialization with error handling
  Serial.println(F("Initializing AHT20..."));
  aht_ok = false;
  
  for (int i = 0; i < 3; i++) {
    Serial.print(F("AHT20 attempt "));
    Serial.print(i + 1);
    Serial.print(F("/3... "));
    Serial.flush();
    
    if (aht.begin()) {
      aht_ok = true;
      Serial.println(F("SUCCESS"));
      break;
    } else {
      Serial.println(F("FAILED"));
      delay(1000);
    }
  }
  
  if (!aht_ok) {
    Serial.println(F("AHT20 initialization FAILED"));
  }
  
  // BMP280 initialization with error handling
  Serial.println(F("Initializing BMP280..."));
  bmp_ok = false;
  
  uint8_t addresses[] = {0x76, 0x77};
  
  for (int addr = 0; addr < 2 && !bmp_ok; addr++) {
    for (int i = 0; i < 3; i++) {
      Serial.print(F("BMP280 at 0x"));
      Serial.print(addresses[addr], HEX);
      Serial.print(F(" attempt "));
      Serial.print(i + 1);
      Serial.print(F("/3... "));
      Serial.flush();
      
      if (bmp.begin(addresses[addr])) {
        bmp_ok = true;
        Serial.println(F("SUCCESS"));
        
        // Configure BMP280 for reliable operation
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                       Adafruit_BMP280::SAMPLING_X2,
                       Adafruit_BMP280::SAMPLING_X8,
                       Adafruit_BMP280::FILTER_X4,
                       Adafruit_BMP280::STANDBY_MS_250);
        
        Serial.println(F("BMP280 configured"));
        break;
      } else {
        Serial.println(F("FAILED"));
        delay(500);
      }
    }
  }
  
  if (!bmp_ok) {
    Serial.println(F("BMP280 initialization FAILED"));
  }
  
  Serial.println(F("=== Sensor Initialization Complete ==="));
  Serial.flush();
}

void loop() {
  unsigned long current_time = millis();
  
  // Watchdog reset
  if (current_time - last_watchdog_reset > 25000) {  // Reduced from 30000
    Serial.println(F("WATCHDOG: Recovery attempt..."));
    initSensors();
    last_watchdog_reset = current_time;
  }
  
  // Handle serial commands
  handleSerialCommands();
  
  // Check system readiness
  if (!system_ready) {
    Serial.println(F("System not ready, reinitializing..."));
    initSensors();
    system_ready = true;
    delay(500);  // Reduced delay
    return;
  }
  
  loop_count++;
  
  // Periodic sensor health check
  if (current_time - last_sensor_check > SENSOR_CHECK_INTERVAL) {
    checkSensors();
    last_sensor_check = current_time;
  }
  
  // Heartbeat
  if (current_time - last_heartbeat > HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    last_heartbeat = current_time;
  }
  
  // Main sensor readings
  performSensorReadings();
  
  // LED blink to indicate activity (less frequent)
  if (loop_count % 20 == 0) {
    digitalWrite(LED_PIN, HIGH);
    delay(25);  // Reduced blink duration
    digitalWrite(LED_PIN, LOW);
  }
  
  // Main loop delay
  delay(MAIN_LOOP_DELAY);
  
  // Update runtime
  status.total_runtime = millis() - boot_time;
  
  // Reset watchdog
  last_watchdog_reset = current_time;
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == F("PING")) {
      Serial.println(F("PONG"));
    } else if (command == F("STATUS")) {
      printStatus();
    } else if (command == F("RESET")) {
      Serial.println(F("RESETTING_SYSTEM"));
      delay(50);
      resetSystem();
    } else if (command == F("RECONNECT")) {
      Serial.println(F("RECONNECTING_SENSORS"));
      initSensors();
    } else if (command == F("HEARTBEAT")) {
      sendHeartbeat();
    } else if (command == F("INFO")) {
      printSystemInfo();
    } else if (command == F("LIGHT_CAL")) {
      calibrateLightSensor();
    } else if (command.length() > 0) {
      Serial.print(F("UNKNOWN_COMMAND: "));
      Serial.println(command);
    }
  }
}

void printStatus() {
  Serial.print(F("AHT20:"));
  Serial.print(aht_ok ? F("OK") : F("FAIL"));
  Serial.print(F(",BMP280:"));
  Serial.print(bmp_ok ? F("OK") : F("FAIL"));
  Serial.print(F(",TEMT6000:OK"));
  Serial.print(F(",LOOPS:"));
  Serial.print(loop_count);
  Serial.print(F(",UPTIME:"));
  Serial.print(millis() / 1000);
  Serial.print(F(",RAM:"));
  Serial.println(freeRam());
}

void printSystemInfo() {
  Serial.println(F("=== System Information ==="));
  Serial.print(F("Uptime: "));
  Serial.print(millis() / 1000);
  Serial.println(F(" seconds"));
  Serial.print(F("Loop count: "));
  Serial.println(loop_count);
  Serial.print(F("Reset count: "));
  Serial.println(status.reset_count);
  Serial.print(F("Sensor failures: "));
  Serial.println(status.sensor_failures);
  Serial.print(F("Light readings: "));
  Serial.println(status.light_readings_count);
  Serial.print(F("Free RAM: "));
  Serial.print(freeRam());
  Serial.println(F(" bytes"));
  Serial.println(F("=== End System Information ==="));
}

void sendHeartbeat() {
  Serial.print(F("HEARTBEAT:"));
  Serial.print(loop_count);
  Serial.print(F(",RAM:"));
  Serial.print(freeRam());
  Serial.print(F(",UPTIME:"));
  Serial.print(millis() / 1000);
  Serial.print(F(",LIGHT:"));
  Serial.println(currentReading.light_lux, 1);
}

void performSensorReadings() {
  // Clear current reading
  memset(&currentReading, 0, sizeof(currentReading));
  currentReading.timestamp = millis() / 1000;
  
  // Read analog sensors first (fastest)
  currentReading.mq9_raw = analogRead(mq9_analog);
  currentReading.light_raw = analogRead(temt6000_analog);
  
  // Convert light sensor reading to lux
  currentReading.light_lux = convertToLux(currentReading.light_raw);
  status.light_readings_count++;
  
  // Read AHT20 with error handling
  sensors_event_t hum, tempAHT;
  bool aht_reading_ok = false;
  
  if (aht_ok) {
    aht_reading_ok = aht.getEvent(&hum, &tempAHT);
    if (aht_reading_ok) {
      currentReading.temp_aht = tempAHT.temperature;
      currentReading.humidity = hum.relative_humidity;
    } else {
      Serial.println(F("AHT20_READ_ERROR"));
    }
  }
  
  // Read BMP280 with error handling
  bool bmp_reading_ok = false;
  
  if (bmp_ok) {
    currentReading.temp_bmp = bmp.readTemperature();
    currentReading.pressure = bmp.readPressure() / 100.0;
    
    // Validate readings
    if (currentReading.temp_bmp > -40 && currentReading.temp_bmp < 85 && 
        currentReading.pressure > 300 && currentReading.pressure < 1100) {
      bmp_reading_ok = true;
    } else {
      Serial.println(F("BMP280_INVALID_DATA"));
    }
  }
  
  // Output formatted data (optimized for speed)
  Serial.print(F("DATA:"));
  Serial.print(currentReading.timestamp);
  Serial.print(F(","));
  Serial.print(loop_count);
  Serial.print(F(","));
  
  if (aht_ok && aht_reading_ok) {
    Serial.print(currentReading.temp_aht, 2);
    Serial.print(F(","));
    Serial.print(currentReading.humidity, 2);
  } else {
    Serial.print(F("0,0"));
  }
  Serial.print(F(","));
  
  if (bmp_ok && bmp_reading_ok) {
    Serial.print(currentReading.temp_bmp, 2);
    Serial.print(F(","));
    Serial.print(currentReading.pressure, 2);
  } else {
    Serial.print(F("0,0"));
  }
  Serial.print(F(","));
  
  Serial.print(currentReading.mq9_raw);
  Serial.print(F(","));
  Serial.print(currentReading.light_raw);
  Serial.print(F(","));
  Serial.println(currentReading.light_lux, 1);
}

float convertToLux(int raw_value) {
  // Convert ADC reading to voltage
  float voltage = (raw_value * TEMT6000_VOLTAGE_REF) / TEMT6000_RESOLUTION;
  
  // Approximate conversion to lux based on TEMT6000 characteristics
  // This is a simplified linear approximation
  // For more accurate results, use a lookup table or polynomial fitting
  float lux = voltage * 200.0;  // Approximate conversion factor
  
  return lux;
}

void calibrateLightSensor() {
  Serial.println(F("=== Light Sensor Calibration ==="));
  Serial.println(F("Reading light sensor for 10 seconds..."));
  
  int min_reading = 1023;
  int max_reading = 0;
  int sum = 0;
  int count = 0;
  
  unsigned long start_time = millis();
  
  while (millis() - start_time < 10000) {
    int reading = analogRead(temt6000_analog);
    min_reading = min(min_reading, reading);
    max_reading = max(max_reading, reading);
    sum += reading;
    count++;
    delay(100);
  }
  
  int average = sum / count;
  
  Serial.print(F("Min reading: "));
  Serial.println(min_reading);
  Serial.print(F("Max reading: "));
  Serial.println(max_reading);
  Serial.print(F("Average: "));
  Serial.println(average);
  Serial.print(F("Estimated lux: "));
  Serial.println(convertToLux(average), 1);
  Serial.println(F("=== Calibration Complete ==="));
}

void checkSensors() {
  Serial.println(F("=== Periodic Sensor Check ==="));
  
  bool sensors_changed = false;
  
  // Check AHT20
  if (!aht_ok) {
    Serial.println(F("Attempting AHT20 reconnection..."));
    if (aht.begin()) {
      aht_ok = true;
      sensors_changed = true;
      Serial.println(F("AHT20 reconnected successfully!"));
    } else {
      Serial.println(F("AHT20 reconnection failed"));
    }
  }
  
  // Check BMP280
  if (!bmp_ok) {
    Serial.println(F("Attempting BMP280 reconnection..."));
    if (bmp.begin(0x76) || bmp.begin(0x77)) {
      bmp_ok = true;
      sensors_changed = true;
      Serial.println(F("BMP280 reconnected successfully!"));
    } else {
      Serial.println(F("BMP280 reconnection failed"));
    }
  }
  
  // Test light sensor
  int light_test = analogRead(temt6000_analog);
  Serial.print(F("Light sensor test: "));
  Serial.print(light_test);
  Serial.print(F(" ("));
  Serial.print(convertToLux(light_test), 1);
  Serial.println(F(" lux)"));
  
  if (sensors_changed) {
    status.sensor_failures++;
    saveSystemStatus();
  }
  
  Serial.println(F("=== Sensor Check Complete ==="));
}

void resetSystem() {
  Serial.println(F("Performing system reset..."));
  delay(50);
  
  // Software reset
  asm volatile ("  jmp 0");
}

void loadSystemStatus() {
  EEPROM.get(0, status);
  
  // Check if EEPROM is initialized
  if (status.first_boot != true) {
    status.first_boot = true;
    status.total_runtime = 0;
    status.reset_count = 0;
    status.sensor_failures = 0;
    status.light_readings_count = 0;
    saveSystemStatus();
  }
}

void saveSystemStatus() {
  EEPROM.put(0, status);
}

int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}