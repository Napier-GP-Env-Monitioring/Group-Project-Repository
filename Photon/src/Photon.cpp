#include "SparkFun_SHTC3.h"

SHTC3 mySHTC3;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  mySHTC3.begin();
}

void loop() {
  mySHTC3.update();

  if (mySHTC3.lastStatus == SHTC3_Status_Nominal) {
    Serial.print("RH: ");
    Serial.print(mySHTC3.toPercent());
    Serial.print("%  Temp: ");
    Serial.print(mySHTC3.toDegC());
    Serial.println(" C");
  } else {
    Serial.println("Sensor error");
  }

  delay(1000);
}