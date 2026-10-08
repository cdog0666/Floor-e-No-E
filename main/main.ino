#include <Wire.h>
#include <LSM6DS3.h>
#include <bluefruit.h>
#include <math.h>

// =====================================================
// ACCELEROMETER
// =====================================================
LSM6DS3 myIMU(I2C_MODE, 0x6A);

// =====================================================
// GAME SETTINGS
// =====================================================
const float IMPACT_THRESHOLD = 2.5;
const float STILLNESS_THRESHOLD = 0.15;
const unsigned long RESET_TIME = 10000;

// Battery update interval (ms)
const unsigned long BATTERY_UPDATE_INTERVAL = 15000;

// =====================================================
// BLE SERVICES & CHARACTERISTICS
// =====================================================
BLEService balloonService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLECharacteristic gameEvent("19B10001-E8F2-537E-4F6C-D104768A1214");
BLEBas blebas; // Standard BLE Battery Service

// =====================================================
// GAME STATE
// =====================================================
bool gameOver = false;
unsigned long gameOverTime = 0;
unsigned long lastHitTime = 0; 
unsigned long lastBatteryCheck = 0;

// Variables for Soft Landing Detection
float prevAx = 0, prevAy = 0, prevAz = 0;
unsigned long stillStartTime = 0;

// =====================================================
// HELPER FUNCTIONS
// =====================================================

// Safely measure battery percentage without locking ADC / BLE stack
uint8_t readBatteryPercentage() {
  #ifdef PIN_VBAT
    int vbatPin = PIN_VBAT;
  #else
    int vbatPin = A7;
  #endif

  #ifdef VBAT_ENABLE
    pinMode(VBAT_ENABLE, OUTPUT);
    digitalWrite(VBAT_ENABLE, LOW); // Enable battery pin reading circuit
    delay(2);
  #endif

  // Configure ADC safely for nRF52
  analogReadResolution(10);
  int raw = analogRead(vbatPin);

  #ifdef VBAT_ENABLE
    digitalWrite(VBAT_ENABLE, HIGH); // Disable reading circuit to save power
  #endif

  // 3.6V internal ref with 1/5 divider on Adafruit Feather nRF52
  float measuredvbat = raw * (3.6 / 1024.0) * 2;
  
  if (measuredvbat >= 4.2) return 100;
  if (measuredvbat <= 3.3) return 0;
  
  // FIX: Fixed syntax error in calculation
  return (uint8_t)((measuredvbat - 3.3) / (4.2 - 3.3) * 100);
}

void flashLed(int durationMs) {
  digitalWrite(LED_BLUE, HIGH);
  delay(durationMs);
  digitalWrite(LED_BLUE, LOW);
}

void sendEvent(const char* event) {
  if (Bluefruit.connected()) {
    gameEvent.notify((uint8_t*)event, strlen(event));
    flashLed(100); 
  }
}

float measureVariance(int samples) {
  float sumX = 0, sumY = 0, sumZ = 0;
  float ax_arr[20], ay_arr[20], az_arr[20];

  if (samples > 20) samples = 20;

  for (int i = 0; i < samples; i++) {
    ax_arr[i] = myIMU.readFloatAccelX();
    ay_arr[i] = myIMU.readFloatAccelY();
    az_arr[i] = myIMU.readFloatAccelZ();
    sumX += ax_arr[i];
    sumY += ay_arr[i];
    sumZ += az_arr[i];
    delay(10);
  }

  float meanX = sumX / samples;
  float meanY = sumY / samples;
  float meanZ = sumZ / samples;

  float varSum = 0;
  for (int i = 0; i < samples; i++) {
    float diff = (ax_arr[i] - meanX) * (ax_arr[i] - meanX) +
                 (ay_arr[i] - meanY) * (ay_arr[i] - meanY) +
                 (az_arr[i] - meanZ) * (az_arr[i] - meanZ);
    varSum += diff;
  }

  return varSum / samples;
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, LOW);

  // -----------------------------------------------
  // Accelerometer
  // -----------------------------------------------
  if (myIMU.begin() != 0) {
    while (true) delay(100);
  }

  // -----------------------------------------------
  // BLE Setup
  // -----------------------------------------------
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName("BalloonGame");

  // Disable status LED blinking
  Bluefruit.autoConnLed(false);

  // Initialize Battery Service
  blebas.begin();
  blebas.write(readBatteryPercentage());

  // Custom Game Service
  balloonService.begin();

  gameEvent.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  gameEvent.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  gameEvent.setMaxLen(20); 
  gameEvent.begin();

  // FIX: Properly declared char pointer string
  const char* ready = "READY";
  gameEvent.write((uint8_t*)ready, strlen(ready));

  // -----------------------------------------------
  // Advertising Setup
  // -----------------------------------------------
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAG_LE_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(balloonService);
  Bluefruit.Advertising.addService(blebas); 
  Bluefruit.ScanResponse.addName();
  
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244); // in unit of 0.625ms
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0); // 0 = Don't stop advertising after n seconds
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
  unsigned long now = millis();

  // Periodically update battery level safely
  if (now - lastBatteryCheck >= BATTERY_UPDATE_INTERVAL) {
    lastBatteryCheck = now;
    if (Bluefruit.connected()) {
      blebas.notify(readBatteryPercentage());
    }
  }

  // =================================================
  // GAME OVER STATE
  // =================================================
  if (gameOver) {
    if (now - gameOverTime >= RESET_TIME) {
      gameOver = false;
      gameOverTime = 0;
      lastHitTime = now;   
      stillStartTime = 0;  
      sendEvent("RESET");
    }
    delay(10);
    return;
  }

  // =================================================
  // READ ACCELEROMETER
  // =================================================
  float ax = myIMU.readFloatAccelX();
  float ay = myIMU.readFloatAccelY();
  float az = myIMU.readFloatAccelZ();

  float accelerationMagnitude = sqrt((ax * ax) + (ay * ay) + (az * az));

  // =================================================
  // 1. DETECT HARD IMPACT
  // =================================================
  if (accelerationMagnitude >= IMPACT_THRESHOLD) {
    if (now - lastHitTime < 600) {
      return; 
    }

    bool settled = false;
    for (int i = 0; i < 12; i++) {
      if (measureVariance(10) < STILLNESS_THRESHOLD) {
        settled = true;
        break; 
      }
    }

    if (settled) {
      gameOver = true;
      gameOverTime = millis();
      sendEvent("GROUND_HIT");
    } else {
      sendEvent("PLAYER_HIT");
      lastHitTime = millis();
      stillStartTime = 0; 
    }
  }

  // =================================================
  // 2. DETECT SOFT LANDING
  // =================================================
  float deltaMovement = abs(ax - prevAx) + abs(ay - prevAy) + abs(az - prevAz);
  
  if (deltaMovement < 0.15) {
    if (stillStartTime == 0) {
      stillStartTime = now;
    } 
    else if (now - stillStartTime > 500) {
      gameOver = true;
      gameOverTime = millis();
      sendEvent("GROUND_HIT");
    }
  } else {
    stillStartTime = 0;
  }

  prevAx = ax;
  prevAy = ay;
  prevAz = az;

  delay(5);
}