/*
This is the basic skeleton, note that the node must accept all types of data such as cameras (serial), and other sensors.
Optimised storage is also desired.
*/

#include "SparkFun_SHTC3.h"

SHTC3 mySHTC3;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  mySHTC3.begin();
}

void loop() {
  // 1. Get humidity and temperature measurements
  mySHTC3.update();

  // 2. Validate & send message to serial monitor, accessed by command 'particle serial monitor'
  if (mySHTC3.lastStatus == SHTC3_Status_Nominal) {
    Serial.print("RH: ");
    Serial.print(mySHTC3.toPercent());
    Serial.print("%  Temp: ");
    Serial.print(mySHTC3.toDegC());
    Serial.println(" C");
  } else {
    Serial.println("Sensor error");
  }

  // 3. Payload Formatting
  int16_t temperature = mySHTC3.toPercent() * 100
  uint16_t humidity = mySHTC3.toDegC() * 100

  // 4. Save To Photon????

  // 5. LoRa Uplink

  // 6. Sleep Logic (needs power management)
  delay(1000);
}