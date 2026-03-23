import spidev
import time
import RPi.GPIO as GPIO
import socket

# UDP socket (for flask app)
UDP_PORT = 540
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0', UDP_PORT))

# GPIO/SPI
RESET_PIN = 22
CS_PIN = 8
DIO0_PIN = 24

GPIO.setmode(GPIO.BCM)
GPIO.setup(DIO0_PIN, GPIO.IN, pull_up_down=GPIO.PUD_DOWN)

spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 500000

# Registers
REG_VERSION = 0x42
REG_OP_MODE = 0x01
REG_FRF_MSB = 0x06
REG_FRF_MID = 0x07
REG_FRF_LSB = 0x08
IRQ_FLAGS = 0x12

# SPI helpers
def spiRead(register):
    resp = spi.xfer2([register & 0x7F, 0x00])
    return resp[1]

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

# =========================
# CLASSES
# =========================

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
        if not self.payload or len(self.payload) < 3:
            return None, None

        rawTemperature = (self.payload[0] << 8) | self.payload[1]
        temperature = rawTemperature / 100.0
        humidity = self.payload[2]

        return temperature, humidity


class Transmitter:
    def __init__(self):
        self.payload = None

    def transmit(self):
        spiWrite(0x0D, 0x00)  # reset FIFO pointer

        for b in self.payload:
            spiWrite(0x00, b)

        spiWrite(0x22, len(self.payload))  # payload length
        spiWrite(IRQ_FLAGS, 0xFF)  # clear IRQ

        spiWrite(REG_OP_MODE, 0x83)  # TX mode
        time.sleep(0.2)

        spiWrite(REG_OP_MODE, 0x85)  # back to RX mode

    def formatPayload(self, message):
        self.payload = [ord(c) for c in message]


# =========================
# INITIALISATION
# =========================

resetModule()

version = spiRead(REG_VERSION)
if version != 0x12:
    print(f"Failed to detect RFM9x! Version read: {version}")
    exit(1)

print(f"RFM9x detected! Version: 0x{version:X}")

# 🔥 PROPER LORA INIT (MATCHES PHOTON)

spiWrite(REG_OP_MODE, 0x80)  # sleep + LoRa
time.sleep(0.01)

setFrequency(868.0)

# FIFO base addresses
spiWrite(0x0E, 0x00)
spiWrite(0x0F, 0x00)

# LNA boost
spiWrite(0x0C, 0x23)

# Modem config (matches Photon defaults)
spiWrite(0x1D, 0x72)  # BW=125kHz, CR=4/5
spiWrite(0x1E, 0x74)  # SF7, CRC on
spiWrite(0x26, 0x04)

# Sync word
spiWrite(0x39, 0x12)

# Map DIO0 -> RxDone
spiWrite(0x40, 0x00)

# Clear IRQ flags
spiWrite(IRQ_FLAGS, 0xFF)

# Continuous RX mode
spiWrite(REG_OP_MODE, 0x85)


# =========================
# MAIN LOOP
# =========================

def listeningLoop():
    receiver = Receiver()
    transmitter = Transmitter()

    while True:
        if receiver.receive():
            temperature, humidity = receiver.parsePayload()

            if temperature is not None:
                message = f"RASPBERRY PI now has H={humidity}%, T={temperature}C"
                print("Transmission received, sending response...")
            else:
                message = "Error - invalid payload"
        else:
            message = "Error - message not received"
            print("No reception!")

        transmitter.formatPayload(message)
        transmitter.transmit()

        # Optional: send via UDP
        sock.sendto(message.encode(), ("127.0.0.1", UDP_PORT))

        time.sleep(5)


# =========================
# RUN
# =========================

if __name__ == "__main__":
    try:
        listeningLoop()
    except KeyboardInterrupt:
        print("Exiting...")
    finally:
        spi.close()
        GPIO.cleanup()