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
const float IMPACT_THRESHOLD = 7.0;
const float IMPACT_REARM_THRESHOLD = 2.5;
const float HAND_IMPACT_DROP_RATE_THRESHOLD = 0.40f;
const uint8_t PEAK_CONFIRMATION_SAMPLES = 2;
const unsigned long DECAY_SAMPLE_INTERVAL_MS = 10;
const unsigned long IMPACT_COOLDOWN_MS = 600;
const unsigned long RESET_TIME = 10000;

// =====================================================
// BLE
// =====================================================
BLEService balloonService("19B10000-E8F2-537E-4F6C-D104768A1214");

BLECharacteristic gameEvent(
  "19B10001-E8F2-537E-4F6C-D104768A1214"
);

// Battery percentage characteristic
BLECharacteristic batteryLevel(
  "19B10002-E8F2-537E-4F6C-D104768A1214"
);

// =====================================================
// BATTERY SETTINGS
// =====================================================
unsigned long lastBatteryUpdate = 0;
const unsigned long BATTERY_UPDATE_INTERVAL = 5000;

// =====================================================
// GAME STATE
// =====================================================
bool gameOver = false;
unsigned long gameOverTime = 0;
unsigned long lastHitTime = 0;
bool impactArmed = true;
bool classifyingImpact = false;
float impactPeakMagnitude = 0;
float impactFirstPostPeakMagnitude = 0;
uint8_t impactBelowPeakSampleCount = 0;
unsigned long lastDecaySampleTime = 0;

// =====================================================
// BATTERY FUNCTIONS
// =====================================================

// Uses the voltage-reading configuration verified in your test.
float readBatteryVoltage() {
  int raw = analogRead(32);
  return 2.961f * 3.6f * raw / 4096.0f;
}

// Estimate percentage from single-cell lithium battery voltage.
int readBatteryPercent() {
  float voltage = readBatteryVoltage();
  int percent;

  if (voltage >= 4.20f) {
    percent = 100;
  } else if (voltage >= 4.00f) {
    percent = 80 + (int)((voltage - 4.00f) * 100.0f);
  } else if (voltage >= 3.85f) {
    percent = 50 + (int)((voltage - 3.85f) * 200.0f);
  } else if (voltage >= 3.70f) {
    percent = 20 + (int)((voltage - 3.70f) * 200.0f);
  } else if (voltage >= 3.50f) {
    percent = 5 + (int)((voltage - 3.50f) * 75.0f);
  } else {
    percent = 0;
  }

  return constrain(percent, 0, 100);
}

// Update battery value and notify connected website.
void updateBatteryLevel() {
  unsigned long now = millis();

  if (now - lastBatteryUpdate < BATTERY_UPDATE_INTERVAL) {
    return;
  }

  lastBatteryUpdate = now;

  uint8_t percent = (uint8_t)readBatteryPercent();

  // Update the value available through BLE reads.
  batteryLevel.write(&percent, 1);

  // Notify the website of the new percentage.
  if (Bluefruit.connected()) {
    batteryLevel.notify(&percent, 1);
  }
}

// =====================================================
// HELPER FUNCTIONS
// =====================================================

void sendEvent(const char* event) {
  if (Bluefruit.connected()) {
    gameEvent.notify((uint8_t*)event, strlen(event));
  }
}

bool isHandImpact(float peakMagnitude, float nextMagnitude) {
  const float dropRate =
    (peakMagnitude - nextMagnitude) / peakMagnitude;
  return dropRate >= HAND_IMPACT_DROP_RATE_THRESHOLD;
}

void completeImpact(bool handImpact) {
  classifyingImpact = false;
  impactArmed = false;
  if (handImpact) {
    sendEvent("PLAYER_HIT");
  } else {
    gameOver = true;
    gameOverTime = millis();
    sendEvent("GROUND_HIT");
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  // -----------------------------------------------
  // Battery voltage measurement
  // -----------------------------------------------
  pinMode(32, INPUT);
  pinMode(14, OUTPUT);
  digitalWrite(14, LOW);

  analogReference(AR_DEFAULT);
  analogReadResolution(12);

  // -----------------------------------------------
  // Accelerometer
  // -----------------------------------------------
  if (myIMU.begin() != 0) {
    while (true) delay(100);
  }

  // -----------------------------------------------
  // BLE
  // -----------------------------------------------
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName("BalloonGame");

  balloonService.begin();

  // Game event characteristic
  gameEvent.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  gameEvent.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  gameEvent.setMaxLen(20);
  gameEvent.begin();

  const char* ready = "READY";
  gameEvent.write((uint8_t*)ready, strlen(ready));

  // Battery percentage characteristic
  batteryLevel.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  batteryLevel.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  batteryLevel.setFixedLen(1);
  batteryLevel.begin();

  uint8_t initialBattery = (uint8_t)readBatteryPercent();
  batteryLevel.write(&initialBattery, 1);

  // -----------------------------------------------
  // Advertising
  // -----------------------------------------------
  Bluefruit.Advertising.addService(balloonService);
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

  // Keep battery reporting active during game-over.
  updateBatteryLevel();

  // =================================================
  // GAME OVER STATE
  // =================================================
  if (gameOver) {
    if (now - gameOverTime >= RESET_TIME) {
      gameOver = false;
      gameOverTime = 0;
      lastHitTime = now;

      sendEvent("RESET");
    }

    delay(10);
    return;
  }

  if (now - lastDecaySampleTime < DECAY_SAMPLE_INTERVAL_MS) {
    return;
  }
  lastDecaySampleTime = now;

  // =================================================
  // READ ACCELEROMETER
  // =================================================
  float ax = myIMU.readFloatAccelX();
  float ay = myIMU.readFloatAccelY();
  float az = myIMU.readFloatAccelZ();

  float accelerationMagnitude =
    sqrtf((ax * ax) + (ay * ay) + (az * az));

  if (!classifyingImpact) {
    if (!impactArmed &&
        accelerationMagnitude <= IMPACT_REARM_THRESHOLD &&
        now - lastHitTime >= IMPACT_COOLDOWN_MS) {
      impactArmed = true;
    }

    if (impactArmed &&
        accelerationMagnitude >= IMPACT_THRESHOLD &&
        now - lastHitTime >= IMPACT_COOLDOWN_MS) {
      classifyingImpact = true;
      impactArmed = false;
      impactPeakMagnitude = accelerationMagnitude;
      impactFirstPostPeakMagnitude = 0;
      impactBelowPeakSampleCount = 0;
      lastHitTime = now;
    }
  } else {
    if (accelerationMagnitude > impactPeakMagnitude) {
      impactPeakMagnitude = accelerationMagnitude;
      impactFirstPostPeakMagnitude = 0;
      impactBelowPeakSampleCount = 0;
    } else {
      if (impactBelowPeakSampleCount == 0) {
        impactFirstPostPeakMagnitude = accelerationMagnitude;
      }

      if (impactBelowPeakSampleCount < PEAK_CONFIRMATION_SAMPLES) {
        impactBelowPeakSampleCount++;
      }

      if (impactBelowPeakSampleCount >= PEAK_CONFIRMATION_SAMPLES) {
        completeImpact(
          isHandImpact(
            impactPeakMagnitude,
            impactFirstPostPeakMagnitude
          )
        );
      }
    }
  }
}