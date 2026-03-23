"""
Raspberry Pi LoRa receiver/transmitter using spidev (RFM9x)
Pipeline:
Sensor --> Photon --> Transmitter )))[LoRa]))) Receiver --> Raspberry Pi --> MariaDB
"""

'''
todo:
1. put resetModule() & setFrequency() into an initialise function
2. move spiRead() & spiWrite into reciever/transceiver classes
'''

import spidev
import time
#import digitalio
#import board
import RPi.GPIO as GPIO # for DIO0 interrupt
import socket

# UDP socket (for flask app)
UDP_PORT = 540
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0', UDP_PORT))

# GPIO/SPI
RESET_PIN = 25 # pin 22 (GPIO25)
CS_PIN = 8 # pin 24 (CE0)
DIO0_PIN = 22 # pin 18 (GPIO22)
GPIO.setmode(GPIO.BCM)
GPIO.setup(DIO0_PIN, GPIO.IN, pull_up_down=GPIO.PUD_DOWN) #
spi = spidev.SpiDev()
spi.open(0, 0) # bus 0, CE0
spi.max_speed_hz = 500000 # start safe, can increase later

# helper functions for RFM9x registers:

REG_VERSION = 0x42 # chip version
REG_OP_MODE = 0x01 # operating mode (sleep/rx/tx)
REG_FRF_MSB = 0x06 # frequency pt. 1
REG_FRF_MID = 0x07 # frequency pt. 2
REG_FRF_LSB = 0x08 # frequency pt. 3

def spiRead(register):
    resp = spi.xfer2([register & 0x7F, 0x00])
    return resp[1]

def spiWrite(register, value):
    spi.xfer2([register | 0x80, value])

def resetModule(): # important for interrupt pin, pulse RST to stop any bad state 
    GPIO.setup(RESET_PIN, GPIO.OUT)
    GPIO.output(RESET_PIN, GPIO.LOW)
    time.sleep(0.05)
    GPIO.output(RESET_PIN, GPIO.HIGH)
    time.sleep(0.1)

def setFrequency(frequencyMHz):
    frf = int(frequencyMHz * 1000000.0 / 61.03515625)
    spiWrite(REG_FRF_MSB, (frf >> 16) & 0xFF)
    spiWrite(REG_FRF_MID, (frf >> 8) & 0xFF)
    spiWrite(REG_FRF_LSB, frf & 0xFF)

# classes:
class Receiver:
    def __init__(self):
        self.payload = None

    def recieve(self, timeout=5.0):
        startTime = time.time()
        while (time.time() - startTime) < timeout:
            if GPIO.input(DIO0_PIN): # if signal, packet ready
                length = spiRead(0x13) # RegRxNbBytes
                
                # read from correct place
                currentAddr = spiRead(0x10) # RegFifoRxCurrentAddr
                spiWrite(0x0D, currentAddr) # RegFifoAddrPtr - seek to start of packet

                self.payload = [spiRead(0x00) for _ in range(length)]
                print("Packet received!")
                return True
            time.sleep(0.01)
        print("Packet not received!")
        return False

    def parsePayload(self):
        rawTemperature = (self.payload[0] << 8) | self.payload[1]
        temperature = rawTemperature / 100.0
        humidity = self.payload[2]
        return temperature, humidity

class Transmitter:
    def __init__(self):
        self.payload = None

    def transmit(self):
        for b in self.payload:
            spiWrite(0x00, b)
        spiWrite(REG_OP_MODE, 0x83)  # TX mode
        time.sleep(0.1)
        spiWrite(REG_OP_MODE, 0x85)  # back to RX mode

    def formatPayload(self, message):
        self.payload = [ord(c) for c in message]


# module initialisation
resetModule()
version = spiRead(REG_VERSION)
if version != 0x12:
    print(f"Failed to detect RFM9x! Version read: {version}")
    exit(1)
print(f"RFM9x detected! Version: 0x{version:X}")

# set frequency
setFrequency(868.0)  # 868 MHz
spiWrite(REG_OP_MODE, 0x85)  # continuous RX mode

# main loop
def listeningLoop():
    receiver = Receiver()
    transmitter = Transmitter()

    while True:
        if receiver.recieve():
            temperature, humidity = receiver.parsePayload()
            message = f"RASPBERRY PI now has H={humidity}%, T={temperature}C"
            print("Transmittion recieved, sending response...")
        else:
            message = "Error - message not received"
            print("No reception, could not receive packet!")
        transmitter.formatPayload(message)
        transmitter.transmit()
        time.sleep(5)

# start loop
if __name__ == "__main__":
    try:
        listeningLoop()
    except KeyboardInterrupt:
        print("Exiting...")
    finally:
        spi.close()
        GPIO.cleanup()