#include <SPI.h>
#include <LoRa.h>
#include "SparkFun_SHTC3.h"


SHTC3 mySHTC3;
uint packetLength = 3;
bool loraReady = false;

void setup() {
  Serial.begin(115200);
  waitFor(Serial.isConnected, 10000);
  Wire.begin();
  Serial.println("Setup...");
  SPI.begin();
  LoRa.setPins(D10, D3, D2);
  loraReady = LoRa.begin(868E6);
  if (loraReady) {
      Serial.println("Transceiver started successfully!");
  } else {
      Serial.println("Transceiver start failed.");
  }
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
    Serial.println("SHTC3 sensor error.");
  }

  // 3. Payload Formatting

  /*
  Unique Identifier = 12B
  Timestamp = 4B
  Temperature = 2B
  Humidity = 1B
  Pressure = 2B
  Soil Moisture = 1B
  GPS Latitude = 3B
  GPS Longitude = 3B
  Flags = 1B
  CRC / Checksum = 2B


  Total = 33 Bytes + overhead
  */

  int16_t temperature = mySHTC3.toDegC() * 100;
  uint8_t humidity = (uint8_t)mySHTC3.toPercent();

  uint8_t payload[3];
  payload[0] = (temperature >> 8) & 0xFF;  // Temperature A
  payload[1] = temperature & 0xFF;         // Temperature B
  payload[2] = humidity & 0xFF;            // Humidity

  // 4. Save To Photon????

  // 5. LoRa 
  if (loraReady) {
    // 5.1 Transmit packet
    LoRa.beginPacket();
    LoRa.write(payload, sizeof(payload));
    LoRa.endPacket();
    Serial.println("Packet sent!");

    // 5.2 Wait
    delay(1000);
    // 5.3 Read from Raspberry Pi
    char received[64];
    
    int len = 0;
    int packetSize = LoRa.parsePacket();
    if (packetSize > 0) {
      while (LoRa.available() && len < 64) {
        received[len++] = (char)LoRa.read();
      }
      Serial.print("\nResponse Recieved! - Message: ");
      Serial.println(received);
      Serial.print("RSSI: ");
      Serial.println(LoRa.packetRssi());
      Serial.print("SNR: ");
      Serial.println(LoRa.packetSnr());
      Serial.println();
    } 
    else {
      Serial.println("Failed to receive response.");
    }
  }
  else {
    Serial.println("Transmittion/Reception unavailable.");
  }

  // 6. Sleep Logic (needs power management)
  delay(4000); // Sleep for 5s, consider using 'System.sleep(5)' as this turns the photon off.
}