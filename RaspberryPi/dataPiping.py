import spidev
import time
import RPi.GPIO as GPIO
import socket

'''

HANDSHAKE PLAN

1. Photon transmits ID and listens for Pi (random intervals as reliability bonus????)
2. Pi records and sends acknowledgement, telling Photon to stop
3. Pi continues to loop, requesting readings one at a time

'''

# UDP socket (for flask app)
UDP_PORT = 540
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0', UDP_PORT))

# GPIO/SPI
RESET_PIN = 25
CS_PIN = 8
DIO0_PIN = 22

GPIO.setmode(GPIO.BCM)
GPIO.setup(DIO0_PIN, GPIO.IN, pull_up_down=GPIO.PUD_DOWN)

spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 500000 # can change later

# Registers
REG_VERSION = 0x42 # version
REG_OP_MODE = 0x01 # operation mode (RX/TX)
REG_FRF_MSB = 0x06 # frequency 1
REG_FRF_MID = 0x07 # frequency 2
REG_FRF_LSB = 0x08 # frequency 3
IRQ_FLAGS = 0x12

# -- functions --
def spiRead(register):
    response = spi.xfer2([register & 0x7F, 0x00])
    return response[1]

def spiWrite(register, value):
    spi.xfer2([register | 0x80, value])

def resetModule():
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

# -- classes --
class Receiver:
    def __init__(self):
        self.payload = None

    def receive(self, timeout=5.0):
        startTime = time.time()

        while (time.time() - startTime) < timeout:
            irq = spiRead(IRQ_FLAGS)
            # DEBUG
            print(f"IRQ: {irq}")

            if irq & 0x40:  # RxDone
                length = spiRead(0x13)

                currentAddr = spiRead(0x10)
                spiWrite(0x0D, currentAddr)

                self.payload = [spiRead(0x00) for _ in range(length)]
                print("Packet received!")
                print("RAW:", self.payload)

                spiWrite(IRQ_FLAGS, 0xFF)  # clear IRQ
                return True

            time.sleep(0.01)

        print("Packet not received!")
        return False

    def parsePayload(self):
        if not self.payload or len(self.payload) < 31:
            return None, None
        
        deviceID = ''
        i = 0
        while (i < 12):
            deviceID += str(self.payload[i])
            i += 1

        timestamp = (self.payload[12] << 24) | (self.payload[13] << 16) | (self.payload[14] << 8) | self.payload[15]
        temperature = ((self.payload[16] << 8) | self.payload[17]) / 100.0
        humidity = self.payload[18]
        pressure = (self.payload[19] << 8) | self.payload[20]
        soilMoisture = self.payload[21]
        latitude = (self.payload[22] << 16) | (self.payload[23] << 8) | self.payload[24]
        longitude = (self.payload[25] << 16) | (self.payload[26] << 8) | self.payload[27]
        flags = self.payload[28]
        crc = (self.payload[29] << 8) | self.payload[30]

        print(deviceID)
        return deviceID, timestamp, temperature, humidity, pressure, soilMoisture, latitude, longitude, flags, crc

class Transmitter:
    def __init__(self):
        self.payload = None

    def transmit(self):
        spiWrite(0x0D, 0x00) # reset FIFO pointer

        for b in self.payload:
            spiWrite(0x00, b)

        spiWrite(0x22, len(self.payload)) # payload length
        spiWrite(IRQ_FLAGS, 0xFF)  # clear IRQ

        spiWrite(REG_OP_MODE, 0x83) # TX mode
        time.sleep(0.2)

        spiWrite(REG_OP_MODE, 0x85) # back to RX mode

    def formatPayload(self, message):
        self.payload = [ord(c) for c in message]

# -- initialisation --
resetModule()

version = spiRead(REG_VERSION)
if version != 0x12:
    print(f"Failed to detect RFM9x! Version read: {version}")
    exit(1)

print(f"RFM9x detected! Version: 0x{version:X}")

# LoRa init (match photon) - buffer, modem, LNA, sync word, 
spiWrite(REG_OP_MODE, 0x80) # turn on LoRa
time.sleep(0.01)
setFrequency(868.0)

# set buffers to zero
spiWrite(0x0E, 0x00)
spiWrite(0x0F, 0x00)

spiWrite(0x0C, 0x23) # LNA boost for better reception

# Modem config (matches Photon defaults)
spiWrite(0x1D, 0x72) # Bandwidth=125kHz, Coding Rate=4/5
spiWrite(0x1E, 0x74) # Spreading factor 7, CRC on
spiWrite(0x26, 0x04)

spiWrite(0x39, 0x12) # Sync word (like an ID, allows devices to hear each other)

spiWrite(0x40, 0x00) # Map DIO0 -> RxDone

spiWrite(IRQ_FLAGS, 0xFF) # Reset all notifications

spiWrite(REG_OP_MODE, 0x85) # Continuous RX mode

# -- main loop --
receiver = Receiver()
transmitter = Transmitter()
deviceIDs = []
def listeningLoop():
    while True:
        if receiver.receive():
            deviceID, timestamp, temperature, humidity, pressure, soilMoisture, latitude, longitude, flags, crc = receiver.parsePayload()
            if not (deviceID in deviceIDs):
                deviceIDs.append(deviceID)
                print(f"Sensor registered {deviceID} as Device{len(deviceIDs)}")
            if temperature is not None:
                message = f"PI recieved: TS={timestamp}, LAT={latitude}, LON={longitude}" # causes error: Response Recieved! - Message: iӵ0u$
                print("Transmission received, sending response...")
            else:
                message = "Error - invalid payload"
        else:
            message = "Error - message not received"
            print("No reception!")

        transmitter.formatPayload(message)
        transmitter.transmit()

        time.sleep(5)

if __name__ == "__main__":
    try:
        listeningLoop()
    except KeyboardInterrupt:
        print("Exiting...")
    finally:
        spi.close()
        GPIO.cleanup()