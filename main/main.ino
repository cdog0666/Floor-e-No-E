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

// Acceleration below this is considered possible free fall
const float FREE_FALL_THRESHOLD = 0.45;

// Acceleration required for a ground collision
const float GROUND_IMPACT_THRESHOLD = 2.5;

// Balloon must be falling for at least this long
// before an impact can count as the ground.
const unsigned long FALL_MIN_TIME = 150;

// How long game-over lasts
const unsigned long RESET_TIME = 10000;

// =====================================================
// BLE
// =====================================================

// Custom BLE service
BLEService balloonService(
  "19B10000-E8F2-537E-4F6C-D104768A1214"
);

// Characteristic sends game events
BLECharacteristic gameEvent(
  "19B10001-E8F2-537E-4F6C-D104768A1214"
);

// =====================================================
// GAME STATE
// =====================================================

bool possibleFall = false;
bool gameOver = false;

unsigned long fallStartTime = 0;
unsigned long gameOverTime = 0;

// =====================================================
// SEND BLE EVENT
// =====================================================

void sendEvent(const char* event) {

  gameEvent.notify(
    (uint8_t*)event,
    strlen(event)
  );
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  // -----------------------------------------------
  // Accelerometer
  // -----------------------------------------------

  if (myIMU.begin() != 0) {

    // Sensor failed.
    // Stop here.
    while (true) {
      delay(100);
    }
  }

  // -----------------------------------------------
  // BLE
  // -----------------------------------------------

  Bluefruit.begin();

  Bluefruit.setTxPower(4);

  Bluefruit.setName("BalloonGame");

  balloonService.begin();

  gameEvent.setProperties(
    CHR_PROPS_READ |
    CHR_PROPS_NOTIFY
  );

  gameEvent.setPermission(
    SECMODE_OPEN,
    SECMODE_NO_ACCESS
  );

  gameEvent.setFixedLen(10);

  gameEvent.begin();

  // Initial state
  const char* ready = "READY";

  gameEvent.write(
    (uint8_t*)ready,
    strlen(ready)
  );

  // Start advertising
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

  // =================================================
  // GAME OVER STATE
  // =================================================

  if (gameOver) {

    // Wait 10 seconds
    if (now - gameOverTime >= RESET_TIME) {

      // Reset everything
      gameOver = false;
      possibleFall = false;

      fallStartTime = 0;
      gameOverTime = 0;

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
  float accelerationMagnitude =
    sqrt(
      (ax * ax) +
      (ay * ay) +
      (az * az)
    );

  // =================================================
  // DETECT POSSIBLE FALL
  // =================================================

  if (accelerationMagnitude < FREE_FALL_THRESHOLD) {

    if (!possibleFall) {

      possibleFall = true;

      fallStartTime = now;
    }
  }

  // =================================================
  // CHECK FOR GROUND IMPACT
  // =================================================

  if (possibleFall) {

    unsigned long fallTime =
      now - fallStartTime;

    // Balloon must have been falling first
    if (fallTime >= FALL_MIN_TIME) {

      // Strong impact after falling
      if (accelerationMagnitude >=
          GROUND_IMPACT_THRESHOLD) {

        // GAME OVER
        gameOver = true;

        gameOverTime = now;

        // Tell website
        sendEvent("GROUND_HIT");

        return;
      }
    }

    // =================================================
    // CANCEL FALSE FALL
    // =================================================

    // If the balloon recovers quickly,
    // assume it was just hit.
    if (accelerationMagnitude > 0.8 &&
        accelerationMagnitude <
        GROUND_IMPACT_THRESHOLD) {

      if (fallTime < FALL_MIN_TIME) {

        possibleFall = false;
      }
    }
  }

  delay(5);
}