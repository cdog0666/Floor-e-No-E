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
// Acceleration required to trigger an impact event
const float IMPACT_THRESHOLD = 2.5;

// Variance below this means the balloon is lying still on the floor
const float STILLNESS_THRESHOLD = 0.15;

// How long game-over lasts (milliseconds)
const unsigned long RESET_TIME = 10000;

// Battery update interval (ms)
const unsigned long BATTERY_UPDATE_INTERVAL = 10000;

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
unsigned long lastHitTime = 0; // Prevents spamming a single hit
unsigned long lastBatteryCheck = 0;

// Variables for Soft Landing Detection
float prevAx = 0, prevAy = 0, prevAz = 0;
unsigned long stillStartTime = 0;

// =====================================================
// HELPER FUNCTIONS
// =====================================================

// Flash LED once when an event occurs
void flashLed(int durationMs) {
  digitalWrite(LED_BLUE, HIGH);
  delay(durationMs);
  digitalWrite(LED_BLUE, LOW);
}

void sendEvent(const char* event) {
  // Only try to send if a device is actually connected
  if (Bluefruit.connected()) {
    gameEvent.notify((uint8_t*)event, strlen(event));
    flashLed(100); // Visual indicator only when meaningful events trigger
  }
}

// Reads battery level percentage (Feather nRF52 VBAT pin on A7)
uint8_t readBatteryPercentage() {
  #ifdef PIN_VBAT
    float raw = analogRead(PIN_VBAT);
  #else
    float raw = analogRead(A7);
  #endif

  // ADC conversion for Feather nRF52 (3.3V ref, 10-bit ADC, divider)
  float measuredvbat = raw * (3.6 / 1024.0) * 2;
  
  // Convert voltage (approx 3.3V to 4.2V) to percentage (0 - 100%)
  if (measuredvbat >= 4.2) return 100;
  if (measuredvbat <= 3.3) return 0;
  return (uint8_t)((measuredvbat - 3.3) / (4.2 - 3.3) * 100);
}

// Samples the accelerometer to see if the board is tumbling or still
float measureVariance(int samples) {
  float sumX = 0, sumY = 0, sumZ = 0;
  float ax_arr[20], ay_arr[20], az_arr[20]; // Cap at 20 samples max

  if (samples > 20) samples = 20;

  for (int i = 0; i < samples; i++) {
    ax_arr[i] = myIMU.readFloatAccelX();
    ay_arr[i] = myIMU.readFloatAccelY();
    az_arr[i] = myIMU.readFloatAccelZ(); // Fixed bug: correctly reads Z axis
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
  // BLE Configuration
  // -----------------------------------------------
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName("BalloonGame");

  // Disable default automatic LED flashing (Connection / Advertising LED)
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

  // Initial state setup
  const char* ready = "READY";
  gameEvent.write((uint8_t*)ready, strlen(ready));

  // Start advertising
  Bluefruit.Advertising.addService(balloonService);
  Bluefruit.Advertising.addService(blebas); // Advertise Battery Service
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
  unsigned long now = millis();

  // Periodically update battery level
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
    // Wait for the RESET_TIME duration
    if (now - gameOverTime >= RESET_TIME) {
      // Reset everything
      gameOver = false;
      gameOverTime = 0;
      lastHitTime = now;   // Prevent immediate re-triggering
      stillStartTime = 0;  // Reset soft landing timer

      // Tell website the game has reset
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

  // Calculate total acceleration
  float accelerationMagnitude = sqrt((ax * ax) + (ay * ay) + (az * az));

  // =================================================
  // 1. DETECT HARD IMPACT
  // =================================================
  if (accelerationMagnitude >= IMPACT_THRESHOLD) {
    
    // Ignore rapid double-triggers from the same physical hit
    if (now - lastHitTime < 600) {
      return; 
    }

    // Monitor for up to 1.2 seconds to allow for bouncing
    bool settled = false;
    for (int i = 0; i < 12; i++) {
      if (measureVariance(10) < STILLNESS_THRESHOLD) {
        settled = true;
        break; // Balloon stopped bouncing and is at rest
      }
    }

    // Decide what happened based on settling
    if (settled) {
      // Ground hit
      gameOver = true;
      gameOverTime = millis();
      sendEvent("GROUND_HIT");
    } else {
      // Player hit (tumbling in air)
      sendEvent("PLAYER_HIT");
      lastHitTime = millis();
      stillStartTime = 0; // Reset soft landing timer on hit
    }
  }

  // =================================================
  // 2. DETECT SOFT LANDING (Ground hit without spike)
  // =================================================
  float deltaMovement = abs(ax - prevAx) + abs(ay - prevAy) + abs(az - prevAz);
  
  if (deltaMovement < 0.15) {
    // Board isn't moving. Start timer if not running.
    if (stillStartTime == 0) {
      stillStartTime = now;
    } 
    // Stationary for 500ms -> ground hit
    else if (now - stillStartTime > 500) {
      gameOver = true;
      gameOverTime = millis();
      sendEvent("GROUND_HIT");
    }
  } else {
    // Board moved/rotated -> reset timer
    stillStartTime = 0;
  }

  // Save current values for next comparison
  prevAx = ax;
  prevAy = ay;
  prevAz = az;

  delay(5);
}