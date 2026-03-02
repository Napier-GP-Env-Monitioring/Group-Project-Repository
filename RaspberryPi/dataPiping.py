# MTTQ/NetCat??

"""
Pipeline:                 
Sensor --> Photon --> Transmitter ---[LoRa]---> Reciever --> Raspberry Pi --> MariaDB
                    (Adafruit RF95W)          (Adafruit RF95W)

photon.cpp --> ... --> dataPiping.py --> database.py
"""

# Import libraries
import board
import busio
import digitalio
import adafruit_rfm9x
spi = busio.SPI(board.SCK, MOSI=board.MOSI, MISO=board.MISO)

# Define pins
cs = digitalio.DigitalInOut(board.D5)
reset = digitalio.DigitalInOut(board.D6)

# Transceiver class
rfm9x = adafruit_rfm9x.RFM9x(spi, cs, reset, 915.0)

class PacketReciever:
    info = '' # correct datatype?
    def updatePacket():
        info = rfm9x.receive()
        if info is None:
            print("Packet not recieved")
        else:
            print("Packet recieved")
            # parse?

    def parse():
        # [add logic]
        print()

# Listening Loop
def listeningLoop():
    
    myPacket = PacketReciever()

    while True:
        # 1. Recieve transmittion
        myPacket.updatePacket() 
        
        # 2. Parse
        myPacket.parse() # pass 'timeout=5.0' to wait for 5 seconds, 0.5s by default

        # 3. Add to database
        # [add code]

