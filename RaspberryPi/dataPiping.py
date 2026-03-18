# MTTQ/NetCat??

"""
Pipeline:                 
Sensor --> Photon --> Transmitter )))[LoRa]))) Reciever --> Raspberry Pi --> MariaDB
                    (Adafruit RF95W)          (Adafruit RF95W)

photon.cpp --> dataPiping.py --> database.py
"""

import board
import busio
import digitalio
import adafruit_rfm9x

import socket
import time 

# Setup
spi = busio.SPI(board.SCK, MOSI=board.MOSI, MISO=board.MISO)
UDP_PORT = 540
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0', UDP_PORT)) 

cs = digitalio.DigitalInOut(board.CE0)
reset = digitalio.DigitalInOut(board.D25)
dio = digitalio.DigitalInOut(board.D24)

    # Create rfm9x object
try:
    rfm9x = adafruit_rfm9x.RFM9x(spi, cs, reset, 868.0) # 868MHz
    print("RMF9x detected")
except RuntimeError as e:
    print("Failed to find RMF9x")

class Receiver:
    def __init__(self, payload=None):
        self.payload = payload

    def getPayload(self):
        self.payload = rfm9x.receive(timeout=5.0) # wait 5s
        if not self.payload or len(self.payload) < 3: # CHANGE ONCE PACKET FORMAT IS IMPLEMENTED
            print("Packet not received!")
            return False
        else:
            print("Packet received!")
            return True

    def parse(self):
        rawTemp = (self.payload[0] << 8) | self.payload[1]
        temperature = rawTemp / 100.0
        humidity = self.payload[2]
        return temperature, humidity

class Transmitter:
    def __init__(self, payload=None):
        self.payload = payload

    def formatPayload(self, message): 
        self.payload = message.encode('utf-8')

    def transmitPayload(self):
        rfm9x.send(self.payload)

# Listening Loop
def listeningLoop():
    myReceiver = Receiver()
    myTransmitter = Transmitter()

    while True:
        # 1. Recieve transmittion
        success = myReceiver.getPayload() 
        
        # 2. Configure message
        if success:
            temperature, humidity = myReceiver.parse()
            message = f"RASPBERRY PI now has H={humidity}%, T={temperature}C"
            print("Transmittion recieved, sending response...")
        else:
            message = "Error - message not received"
            print("No reception")

        # 3. Respond
        myTransmitter.formatPayload(message)
        myTransmitter.transmitPayload()

        time.sleep(5.0)

        # 3. Add to database
        # [add logic]

# start loop
listeningLoop()