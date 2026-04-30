#include <SPI.h>
#include <LoRa.h>
#include <Adafruit_GPS.h>
#include "SparkFun_SHTC3.h"

#define GPSSerial Serial1

Adafruit_GPS GPS(&GPSSerial);
SHTC3 mySHTC3;

bool loraReady = false;
uint8_t deviceID[12];

void ExtractDeviceID(uint8_t* out) {
  String id = System.deviceID();
  for (int i = 0; i < 12; i++) {
    String byteStr = id.substring(i * 2, i * 2 + 2);
    out[i] = (uint8_t)strtol(byteStr.c_str(), NULL, 16);
  }
}

class Measurements {
  private:
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

    void ReadAll() {
      ReadSHTC3();
      ReadPressure();
      ReadSoilMoisture();

      Serial.println();
      Serial.println(" Temperature: " + String(temperature / 100.0, 2) + " C");
      Serial.println("    Humidity: " + String(humidity) + " %");
      Serial.println("    Pressure: " + String(pressure) + " Pa");
      Serial.println("SoilMoisture: " + String(soilMoisture));
      Serial.println("    Latitude: " + String(latitude / 100000.0, 5));
      Serial.println("   Longitude: " + String(longitude / 100000.0, 5));
      Serial.println();
    }

    void ReadSHTC3() {
      mySHTC3.update();
      if (mySHTC3.lastStatus == SHTC3_Status_Nominal) {
        temperature = (int16_t)(mySHTC3.toDegC() * 100.0);
        humidity = (uint8_t)(mySHTC3.toPercent());
      } else {
        Serial.println("SHTC3 Error!");
      }
    }

    bool ReadGPS() {
      GPS.read();

      if (GPS.newNMEAreceived()) {
        GPS.parse(GPS.lastNMEA());
      }

      if (GPS.fix) {
        float longitudeRaw = GPS.longitude;
        float latitudeRaw = GPS.latitude;

        int latDegrees = (int)(latitudeRaw / 100);
        float latMinutes = latitudeRaw - (latDegrees * 100);
        float latDecimal = latDegrees + (latMinutes / 60.0);
        if (GPS.lat == 'S') latDecimal = -latDecimal;

        int longDegrees = (int)(longitudeRaw / 100);
        float longMinutes = longitudeRaw - (longDegrees * 100);
        float longDecimal = longDegrees + (longMinutes / 60.0);
        if (GPS.lon == 'W') longDecimal = -longDecimal;

        latitude = (int32_t)((latDecimal + 90.0) * (16777215.0 / 180.0));
        longitude = (int32_t)((longDecimal + 180.0) * (16777215.0 / 360.0));
        return true;
      }

      return false;
    }

    void ReadPressure() {
      pressure = 0; // placeholder
    }

    void ReadSoilMoisture() {
      soilMoisture = 0; // placeholder
    }
};

class Transmitter {
  private:
    uint8_t payload[31];

  public:
    void FormatPayload(const Measurements& myMeasurements) {
      ExtractDeviceID(deviceID);
      uint32_t timestamp = Time.now();

      int16_t temperature = myMeasurements.getTemperature();
      uint8_t humidity = myMeasurements.getHumidity();
      uint16_t pressure = myMeasurements.getPressure();
      uint8_t soilMoisture = myMeasurements.getSoilMoisture();
      int32_t latitude = myMeasurements.getLatitude();
      int32_t longitude = myMeasurements.getLongitude();

      memset(payload, 0, sizeof(payload));

      memcpy(payload, deviceID, 12);

      payload[12] = (timestamp >> 24) & 0xFF;
      payload[13] = (timestamp >> 16) & 0xFF;
      payload[14] = (timestamp >> 8) & 0xFF;
      payload[15] = timestamp & 0xFF;

      payload[16] = (temperature >> 8) & 0xFF;
      payload[17] = temperature & 0xFF;

      payload[18] = humidity;

      payload[19] = (pressure >> 8) & 0xFF;
      payload[20] = pressure & 0xFF;

      payload[21] = soilMoisture;

      payload[22] = (latitude >> 16) & 0xFF;
      payload[23] = (latitude >> 8) & 0xFF;
      payload[24] = latitude & 0xFF;

      payload[25] = (longitude >> 16) & 0xFF;
      payload[26] = (longitude >> 8) & 0xFF;
      payload[27] = longitude & 0xFF;

      payload[28] = 0x00; // flags
      payload[29] = 0x00; // crc placeholder
      payload[30] = 0x00; // crc placeholder
    }

    void Transmit() {
      LoRa.idle();
      LoRa.beginPacket();
      LoRa.write(payload, sizeof(payload));
      LoRa.endPacket();
      LoRa.receive();   // very important
      Serial.println("Packet sent!");
    }
};

class Receiver {
  private:
    uint8_t piDeviceID[12];
    uint8_t status = 0;

  public:
    const uint8_t* getPiDeviceID() const { return piDeviceID; }
    uint8_t getStatus() const { return status; }

    bool Receive(float duration, bool verboseTimeout = false) {
      uint8_t received[13] = {0};
      int len = 0;
      bool isMatch = false;
      status = 0;

      unsigned long start = millis();
      int packetSize = 0;

      while (millis() - start < (unsigned long)(duration * 1000)) {
        packetSize = LoRa.parsePacket();
        if (packetSize > 0) {
          break;
        }
        delay(5);
      }

      if (packetSize <= 0) {
        if (verboseTimeout) {
          Serial.println("No control packet received.");
        }
        return false;
      }

      if (packetSize != 13) {
        Serial.print("Unexpected control packet size: ");
        Serial.println(packetSize);
        while (LoRa.available()) {
          LoRa.read();
        }
        return false;
      }

      while (LoRa.available() && len < 13) {
        received[len++] = (uint8_t)LoRa.read();
      }

      if (len != 13) {
        Serial.print("Incomplete ACK/control packet. Length = ");
        Serial.println(len);
        return false;
      }

      memcpy(piDeviceID, received, 12);
      status = received[12];

      Serial.print("Received control packet. Status = ");
      Serial.println(status);

      isMatch = true;
      for (int i = 0; i < 12; i++) {
        if (piDeviceID[i] != deviceID[i]) {
          isMatch = false;
          break;
        }
      }

      if (!isMatch) {
        Serial.println("Control packet device ID does not match this Photon.");
      }

      return isMatch;
    }
};

Measurements myMeasurements;
Transmitter myTransmitter;
Receiver myReceiver;

void printDeviceID(const uint8_t* idBytes) {
  for (int i = 0; i < 12; i++) {
    if (idBytes[i] < 16) Serial.print("0");
    Serial.print(idBytes[i], HEX);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);
  waitFor(Serial.isConnected, 10000);

  Wire.begin();
  SPI.begin();

  Serial.println("Setup starting...");

  ExtractDeviceID(deviceID);
  Serial.print("Photon device ID: ");
  printDeviceID(deviceID);

  LoRa.setPins(D10, D3, D2);
  loraReady = LoRa.begin(868E6);

  if (!loraReady) {
    Serial.println("Transceiver start failed.");
    System.reset();
  }

  // Match Raspberry Pi settings explicitly
  LoRa.setSyncWord(0x12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setSpreadingFactor(7);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  LoRa.setTxPower(2);   // low power for testing short-range overload
  LoRa.receive();

  Serial.println("LoRa transceiver started successfully!");

  if (mySHTC3.begin() != SHTC3_Status_Nominal) {
    Serial.println("SHTC3 init failed!");
  } else {
    Serial.println("SHTC3 ready.");
  }

  GPS.begin(9600);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
  delay(1000);

  Serial.println("Trying to connect to base node...");

  bool connected = false;

while (!connected) {
  myTransmitter.FormatPayload(myMeasurements);
  myTransmitter.Transmit();

  delay(200);   // <-- ADDED: let Pi process and send ACKs

  bool match = myReceiver.Receive(5.0, true);
  int status = myReceiver.getStatus();

  Serial.print("Match: ");
  Serial.println(match ? "true" : "false");
  Serial.print("Status: ");
  Serial.println(status);

  if (match && status == 2) {
    connected = true;
  } else {
    Serial.println("Handshake not completed, retrying...");
    delay(1000);
  }
}

  Serial.println("Connected to base node!");
}

unsigned long lastPoll = 0;

void loop() {
  myMeasurements.ReadGPS();

  if (millis() - lastPoll > 100) {
    lastPoll = millis();

    bool match = myReceiver.Receive(0.2, false);

    if (match) {
      uint8_t status = myReceiver.getStatus();

      if (status == 4) {
        Serial.println("Pi requested a reading.");

        myMeasurements.ReadAll();
        myTransmitter.FormatPayload(myMeasurements);
        myTransmitter.Transmit();
	
	delay(150); // gives Pi time to switch back to RX and process packet
	
        // wait briefly for success/fail response
        bool gotFinalReply = myReceiver.Receive(1.5, false);
        if (gotFinalReply) {
          uint8_t finalStatus = myReceiver.getStatus();
          Serial.print("Final status from Pi: ");
          Serial.println(finalStatus);

          if (finalStatus == 1) {
            Serial.println("Reading accepted by Pi.");
          } else if (finalStatus == 0) {
            Serial.println("Pi reported failure.");
          }
        } else {
          Serial.println("No final success/fail reply received.");
        }
      } else if (status == 1) {
        Serial.println("Received success status.");
      } else if (status == 0) {
        Serial.println("Received fail status.");
      } else if (status == 2) {
        Serial.println("Received registration acknowledgement.");
      } else {
        Serial.print("Received unknown status: ");
        Serial.println(status);
      }
    }
  }
}
