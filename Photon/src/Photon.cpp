#include <SPI.h>
#include <LoRa.h>
#include "SparkFun_SHTC3.h"
#include <Adafruit_GPS.h>

#define GPSSerial Serial1

Adafruit_GPS GPS(&GPSSerial1)
SHTC3 mySHTC3;
uint packetLength = 3;
bool loraReady = false;
uint8_t deviceID[12];

void extractDeviceID(uint8_t* out) {
    String id = System.deviceID();
    for (int i=0; i<12; i++) {
        String byteStr = id.substring(i*2, i*2+2);
        out[i] = (uint8_t)strtol(byteStr.c_str(), NULL, 16);
    }
}

void setup() {
  Serial.begin(115200);
  waitFor(Serial.isConnected, 10000);
  Wire.begin();
  LoRa.setSyncWord(0x12); // match raspberry pi
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
  GPS.begin(115200)
}

void loop() {
  // MEASUREMENTS
  mySHTC3.update();

  if (GPS.fix) {
    Serial.println("GPS does not have a fix!");
  } else {
    try {
      float longitudeRaw = GPS.longitude;
      float latitudeRaw = GPS.latitude;

      // Convert to decimal degrees
      int longDegrees = (int)(longitudeRaw / 100);
      float longMinutes = longitudeRaw - (degrees * 100);
      float longDecimal = degrees + (minutes / 60.0);

      int latDegrees = (int)(latitudeRaw / 100);
      float latMinutes = latitudeRaw - (degrees * 100);
      float latDecimal = degrees + (minutes / 60.0);

      uint32_t longitude = (uint32_t)(longDecimal * 100000);
      uint32_t latitude = (uint32_t)(latDecimal * 100000);
    }
    catch (...) {
      Serial.println("Error while parsing GPS!");
    }
  }

  // Validate & send message to serial monitor, accessed by command 'particle serial monitor'
  if (mySHTC3.lastStatus == SHTC3_Status_Nominal) { // CHANGE!!!
    Serial.print("RH: ");
    Serial.print(mySHTC3.toPercent());
    Serial.print("%  Temp: ");
    Serial.print(mySHTC3.toDegC());
    Serial.println(" C");
  } else {
    Serial.println("Validation error!");
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

  extractDeviceID(deviceID);
  uint32_t timestamp = Time.now();
  int16_t temperature = mySHTC3.toDegC() * 100;
  uint8_t humidity = (uint8_t)mySHTC3.toPercent();

  uint8_t payload[20];
  payload[0] = deviceID;
  payload[1] = (timestamp >> 24) & 0xFF;
  payload[2] = (timestamp >> 16) & 0xFF;
  payload[3] = (timestamp >> 8) & 0xFF;
  payload[4] = (timestamp >> 0) & 0xFF;
  payload[5] = (temperature >> 8) & 0xFF; 
  payload[6] = temperature & 0xFF;         
  payload[7] = humidity & 0xFF;    
 
  payload[8] = 0x00; // 8 PRESSURE
  payload[9] = 0x00; // 9 PRESSURE
  payload[10] = 0x00; // 10 SOIL MOISTURE

  payload[11] = (latitude >> 16) & 0xFF;
  payload[12] = (latitude >> 8) & 0xFF;
  payload[13] = latitude & 0xFF;
  payload[14] = (longitude>> 16) & 0xFF;
  payload[15] = (longitude >> 8) & 0xFF;
  payload[16] = longitude & 0xFF;

  payload[17] = 0x00; // 18 FLAGS
  payload[18] = 0x00; // 19 CRC/CHECKSUM 1
  payload[19] = 0x00; // 20 CRC/CHECKSUM 2

  // 4. LoRa 
  if (loraReady) {
    // 4.1 Transmit packet
    LoRa.beginPacket();
    LoRa.write(payload, sizeof(payload));
    LoRa.endPacket();
    Serial.println("Packet sent!");

    // 4.2 Wait
    // Poll for up to 10 seconds
    unsigned long start = millis();
    int packetSize = 0;
    while (millis() - start < 10000) {
      packetSize = LoRa.parsePacket();
      if (packetSize > 0) break;
      delay(10);
    }

    // 4.3 Read from Raspberry Pi
    char received[64];
    
    int len = 0;
    //int packetSize = LoRa.parsePacket();
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

  // 5. Sleep Logic (needs power management)
  delay(4000); // Sleep for 5s, consider using 'System.sleep(5)' as this turns the photon off.
}