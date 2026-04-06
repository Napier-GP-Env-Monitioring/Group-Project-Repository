#include <SPI.h>
#include <LoRa.h>
#include "SparkFun_SHTC3.h"
#include <Adafruit_GPS.h>

#define GPSSerial Serial1

Adafruit_GPS GPS(&GPSSerial);
SHTC3 mySHTC3;
uint packetLength = 3;
bool loraReady = false;
uint8_t deviceID[12];


uint32_t longitude;
uint32_t latitude;

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
  GPS.begin(115200);
}

void loop() {
  // MEASUREMENTS
  mySHTC3.update();

  if (GPS.fix) {
    float longitudeRaw = GPS.longitude;
    float latitudeRaw = GPS.latitude;

    // Convert to decimal degrees
    int longDegrees = (int)(longitudeRaw / 100);
    float longMinutes = longitudeRaw - (longDegrees * 100);
    float longDecimal = longDegrees + (longMinutes / 60.0);

    int latDegrees = (int)(latitudeRaw / 100);
    float latMinutes = latitudeRaw - (latDegrees * 100);
    float latDecimal = latDegrees + (latMinutes / 60.0);

    longitude = (uint32_t)(longDecimal * 100000);
    latitude = (uint32_t)(latDecimal * 100000);

  } else {
    Serial.println("GPS does not have a fix!");
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


  Total = 31 Bytes + overhead
  */

  extractDeviceID(deviceID);
  uint32_t timestamp = Time.now();
  int16_t temperature = mySHTC3.toDegC() * 100;
  uint8_t humidity = (uint8_t)mySHTC3.toPercent();

  uint8_t payload[31];

  payload[0] = deviceID[0];
  payload[1] = deviceID[1];
  payload[2] = deviceID[2];
  payload[3] = deviceID[3];
  payload[4] = deviceID[4];
  payload[5] = deviceID[5];
  payload[6] = deviceID[6];
  payload[7] = deviceID[7];
  payload[8] = deviceID[8];
  payload[9] = deviceID[9];
  payload[10] = deviceID[10];
  payload[11] = deviceID[11];

  payload[12] = (timestamp >> 24) & 0xFF;
  payload[13] = (timestamp >> 16) & 0xFF;
  payload[14] = (timestamp >> 8) & 0xFF;
  payload[15] = (timestamp >> 0) & 0xFF;
  payload[16] = (temperature >> 8) & 0xFF; 
  payload[17] = temperature & 0xFF;         
  payload[18] = humidity & 0xFF;    
 
  payload[19] = 0x00; // 8 PRESSURE
  payload[20] = 0x00; // 9 PRESSURE
  payload[21] = 0x00; // 10 SOIL MOISTURE

  payload[22] = (latitude >> 16) & 0xFF;
  payload[23] = (latitude >> 8) & 0xFF;
  payload[24] = latitude & 0xFF;
  payload[25] = (longitude >> 16) & 0xFF;
  payload[26] = (longitude >> 8) & 0xFF;
  payload[27] = longitude & 0xFF;

  payload[28] = 0x00; // 18 FLAGS
  payload[29] = 0x00; // 19 CRC/CHECKSUM 1
  payload[30] = 0x00; // 20 CRC/CHECKSUM 2

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