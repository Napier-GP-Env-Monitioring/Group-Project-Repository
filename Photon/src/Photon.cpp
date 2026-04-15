#include <SPI.h>
#include <LoRa.h>
#include <Adafruit_GPS.h>
#include "SparkFun_SHTC3.h"

#define GPSSerial Serial1
Adafruit_GPS GPS(&GPSSerial);
SHTC3 mySHTC3;
bool connectedToPi = false;
bool loraReady = false;
uint8_t deviceID[12]; // needed?

void ExtractDeviceID(uint8_t* out) { 
  String id = System.deviceID();
  for (int i = 0; i < 12; i++) {
      String byteStr = id.substring(i*2, i*2+2);
      out[i] = (uint8_t)strtol(byteStr.c_str(), NULL, 16);
  }
}

class Measurements { 
    int16_t temperature = 0;
    uint8_t humidity = 0;
    uint16_t pressure = 0;
    uint8_t soilMoisture = 0;
    int32_t latitude = 0;
    int32_t longitude = 0;

  public:
    int16_t getTemperature() const { return temperature; }
    uint8_t getHumidity() const { return humidity; }
    uint16_t getPressure() const { return pressure; }
    uint8_t getSoilMoisture() const { return soilMoisture; }
    int32_t getLatitude() const { return latitude; }
    int32_t getLongitude() const { return longitude; }

    void ReadAll() { // GPS must be read in main loop
      ReadSHTC3();
      ReadPressure();
      ReadSoilMoisture();

      Serial.println();
      Serial.println(" Temperature: " + String(temperature/100) + "C");
      Serial.println("    Humidity: " + String(humidity) + "%");
      Serial.println("    Pressure: " + String(pressure) + "Pa");
      Serial.println("SoilMoisture: " + String(soilMoisture) + "WVC");
      Serial.println("    Latitude: " + String(latitude/100000));
      Serial.println("   Longitude: " + String(longitude/10000));
      Serial.println();
    }

    void ReadSHTC3() {
      mySHTC3.update();
      if (mySHTC3.lastStatus == SHTC3_Status_Nominal) {
        temperature = mySHTC3.toDegC() * 100;
        humidity = (uint8_t)mySHTC3.toPercent();
      } else {
        Serial.println("SHTC3 Error!");
      }
    }

    bool ReadGPS() {  
      GPS.read();
      if (GPS.newNMEAreceived()) {
        GPS.parse(GPS.lastNMEA()); // Parse each complete sentence
        Serial.println(GPS.lastNMEA());
      }

      bool isFixed = GPS.fix;
      if (isFixed) {
        float longitudeRaw = GPS.longitude;
        float latitudeRaw = GPS.latitude;

        // Convert to decimal degrees
        int latDegrees = (int)(latitudeRaw / 100);
        float latMinutes = latitudeRaw - (latDegrees * 100);
        float latDecimal = latDegrees + (latMinutes / 60.0);
        if (GPS.lat == 'S') latDecimal = -latDecimal;

        int longDegrees = (int)(longitudeRaw / 100);
        float longMinutes = longitudeRaw - (longDegrees * 100);
        float longDecimal = longDegrees + (longMinutes / 60.0);
        if (GPS.lon == 'W') longDecimal = -longDecimal;

        latitude = (int32_t)(latDecimal * 100000);
        longitude = (int32_t)(longDecimal * 100000);
        
        return isFixed; // true
      } else {
        return isFixed; // false
      }
    }

    void ReadPressure() { // Placeholder
      
    }

    void ReadSoilMoisture() { // Placeholder

    }
};

class Transmitter {
  private:
    uint8_t payload[31];
    uint8_t flag = 0;

  public:
    void Transmit() {
      // Transmit packet
      LoRa.beginPacket();
      LoRa.write(payload, sizeof(payload));
      LoRa.endPacket();
      Serial.println("Packet sent!");
    }

    void FormatPayload(const Measurements& myMeasurements) {
      ExtractDeviceID(deviceID);
      uint32_t timestamp = Time.now();

      int16_t temperature = myMeasurements.getTemperature();
      uint8_t humidity = myMeasurements.getHumidity();
      uint16_t pressure = myMeasurements.getPressure();
      uint8_t soilMoisture = myMeasurements.getSoilMoisture();
      int32_t latitude = myMeasurements.getLatitude();
      int32_t longitude = myMeasurements.getLongitude();

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

      memcpy(payload, deviceID, 12);

      payload[12] = (timestamp >> 24) & 0xFF;
      payload[13] = (timestamp >> 16) & 0xFF;
      payload[14] = (timestamp >> 8) & 0xFF;
      payload[15] = (timestamp >> 0) & 0xFF;

      payload[16] = (temperature >> 8) & 0xFF; 
      payload[17] = temperature & 0xFF; 
              
      payload[18] = humidity & 0xFF;   

      payload[19] = (pressure >> 8) & 0xFF;
      payload[20] = pressure & 0xFF;

      payload[21] = soilMoisture & 0xFF;

      payload[22] = (latitude >> 16) & 0xFF;
      payload[23] = (latitude >> 8) & 0xFF;
      payload[24] = latitude & 0xFF;

      payload[25] = (longitude >> 16) & 0xFF;
      payload[26] = (longitude >> 8) & 0xFF;
      payload[27] = longitude & 0xFF;

      payload[28] = 0x00; // 18 FLAGS
      payload[29] = 0x00; // 19 CRC/CHECKSUM 1
      payload[30] = 0x00; // 20 CRC/CHECKSUM 2
    }
};

class Receiver {
  private:
    uint8_t piDeviceID[13];
    uint8_t status = 0;
  public:
    const uint8_t* getPiDeviceID() const { return piDeviceID; }
    uint8_t getStatus() const { return status; }
      
    bool Receive(float duration) {
      uint8_t received[13];
      int len = 0;
      bool isMatch = false;

      // Wait, poll for duration
      unsigned long start = millis();
      int packetSize = 0;
      while (millis() - start < (int)(duration*1000)) {
        packetSize = LoRa.parsePacket();
        if (packetSize > 0) break;
      }

      if (packetSize > 0) { // If packet isn't empty
        while (LoRa.available() && len < 13) {
          received[len++] = (uint8_t)LoRa.read();
        }
        
        memcpy(piDeviceID, received, 12);
        status = received[12];

        isMatch = true;
        for (int i = 0; i < 12; i++) {
          if (piDeviceID[i] != deviceID[i]) {
            isMatch = false;
            break;
          }
        }
      }
      else {
        Serial.println("Could not receive response!");
      }
      return isMatch;
    }
};

Measurements myMeasurements;
Transmitter myTransmitter;
Receiver myReceiver;

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);
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
    exit(1);
  }
  mySHTC3.begin(); // add error handling here
  GPS.begin(9600);

  // connect to pi
  Serial.println("Trying to connect to base node...");
  bool connected = false;
  while (!connected) {
    myTransmitter.FormatPayload(myMeasurements);
    myTransmitter.Transmit();
    myReceiver.Receive(0.5);
    if (myReceiver.getStatus() == 2) { // if raspberry pi sends acknowledgement
      connected = true;
      Serial.println("Connected to base node!");
    }
  }
}

unsigned long lastRun = 0;
void loop() {
  myMeasurements.ReadGPS(); // continuously update GPS or will not work, CHECK ----------------------
    if (millis() - lastRun > 100) { // every 0.1s
      lastRun = millis();

      bool match = myReceiver.Receive(0.2);
      if (match && myReceiver.getStatus() == 4) { // if raspberry is talking to this device AND is requesting data (status 4)
        myMeasurements.ReadAll();
        myTransmitter.FormatPayload(myMeasurements);
        myTransmitter.Transmit();
      }
    }
}