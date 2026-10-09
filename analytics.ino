#include <LSM6DS3.h>
#include <bluefruit.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

LSM6DS3 imu(I2C_MODE, 0x6A);

BLEService analyticsService("19B10010-E8F2-537E-4F6C-D104768A1214");
BLECharacteristic accelerationData(
  "19B10011-E8F2-537E-4F6C-D104768A1214"
);

const unsigned long OUTPUT_INTERVAL_MS = 10;
unsigned long lastOutputTime = 0;

void setup() {
  if (imu.begin() != 0) {
    while (true) {
      delay(100);
    }
  }

  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName("XIAO-Analytics");

  analyticsService.begin();

  accelerationData.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  accelerationData.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  accelerationData.setMaxLen(32);
  accelerationData.begin();

  const char initialValue[] = "0.00,0.00,0.00";
  accelerationData.write(
    reinterpret_cast<const uint8_t*>(initialValue),
    sizeof(initialValue) - 1
  );

  Bluefruit.Advertising.addService(analyticsService);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.start(0);
}

void loop() {
  const unsigned long now = millis();
  if (now - lastOutputTime < OUTPUT_INTERVAL_MS) {
    return;
  }
  lastOutputTime = now;

  const float x = imu.readFloatAccelX();
  const float y = imu.readFloatAccelY();
  const float z = imu.readFloatAccelZ();
  const float magnitude = sqrtf(x * x + y * y + z * z);

  char output[48];
  const int length = snprintf(
    output,
    sizeof(output),
    "%.2f,%.2f,%.2f,%.2f",
    x,
    y,
    z,
    magnitude
  );

  if (length <= 0 || length >= static_cast<int>(sizeof(output))) {
    return;
  }

  accelerationData.write(
    reinterpret_cast<const uint8_t*>(output),
    static_cast<uint16_t>(length)
  );

  if (Bluefruit.connected()) {
    accelerationData.notify(
      reinterpret_cast<const uint8_t*>(output),
      static_cast<uint16_t>(length)
    );
  }
}