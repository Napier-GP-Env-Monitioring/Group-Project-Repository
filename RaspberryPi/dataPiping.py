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
def spi_read(register):
    response = spi.xfer2([register & 0x7F, 0x00])
    return response[1]

def spi_write(register, value):
    if isinstance(value, (bytes, bytearray, list)):
        spi.xfer2([register | 0x80] + list(value))
    else:
        spi.xfer2([register | 0x80, int(value)])

def reset_module():
    GPIO.setup(RESET_PIN, GPIO.OUT)
    GPIO.output(RESET_PIN, GPIO.LOW)
    time.sleep(0.05)
    GPIO.output(RESET_PIN, GPIO.HIGH)
    time.sleep(0.1)

def set_frequency(frequency_mhz):
    frf = int(frequency_mhz * 1000000.0 / 61.03515625)
    spi_write(REG_FRF_MSB, (frf >> 16) & 0xFF)
    spi_write(REG_FRF_MID, (frf >> 8) & 0xFF)
    spi_write(REG_FRF_LSB, frf & 0xFF)

# -- classes --
class Receiver:
    def __init__(self):
        self.payload = None

    def receive(self, timeout=5.0):
        start_time = time.time()

        while (time.time() - start_time) < timeout:
            irq = spi_read(IRQ_FLAGS)
            # DEBUG
            print(f"IRQ: {irq}")

            if irq & 0x40:  # RxDone
                length = spi_read(0x13)

                current_addr = spi_read(0x10)
                spi_write(0x0D, current_addr)

                self.payload = [spi_read(0x00) for _ in range(length)]
                if all(b == 0 for b in self.payload):
                    return
                print("Packet received!")
                print("RAW:", self.payload)

                spi_write(IRQ_FLAGS, 0xFF)  # clear IRQ
                return True

            time.sleep(0.01)

        print("Packet not received!")
        return False

    def parse_payload(self):
        if not self.payload or len(self.payload) < 31:
            return None, None
        
        device_id = ''
        i = 0
        while (i < 12):
            device_id += str(self.payload[i])
            i += 1

        timestamp = (self.payload[12] << 24) | (self.payload[13] << 16) | (self.payload[14] << 8) | self.payload[15]
        temperature = ((self.payload[16] << 8) | self.payload[17]) / 100.0
        humidity = self.payload[18]
        pressure = (self.payload[19] << 8) | self.payload[20]
        soil_moisture = self.payload[21]
        latitude = (self.payload[22] << 16) | (self.payload[23] << 8) | self.payload[24]
        longitude = (self.payload[25] << 16) | (self.payload[26] << 8) | self.payload[27]
        flags = self.payload[28]
        crc = (self.payload[29] << 8) | self.payload[30]

        print(device_id)
        return device_id, timestamp, temperature, humidity, pressure, soil_moisture, latitude, longitude, flags, crc

class Transmitter:
    def __init__(self):
        self.payload = [0]*13

    def transmit(self):
        spi_write(0x0D, 0x00) # reset FIFO pointer

        for b in self.payload:
            spi_write(0x00, b)

        spi_write(0x22, len(self.payload)) # payload length
        spi_write(IRQ_FLAGS, 0xFF)  # clear IRQ

        spi_write(REG_OP_MODE, 0x83) # TX mode
        time.sleep(0.2)

        spi_write(REG_OP_MODE, 0x85) # back to RX mode

    def format_payload(self, id, status):
      self.payload[0] = id[0]
      self.payload[1] = id[1]
      self.payload[2] = id[2]
      self.payload[3] = id[3]
      self.payload[4] = id[4]
      self.payload[5] = id[5]
      self.payload[6] = id[6]
      self.payload[7] = id[7]
      self.payload[8] = id[8]
      self.payload[9] = id[9]
      self.payload[10] = id[10]
      self.payload[11] = id[11]
      self.payload[12] = status

# -- initialisation --
reset_module()

version = spi_read(REG_VERSION)
if version != 0x12:
    print(f"Failed to detect RFM9x! Version read: {version}")
    exit(1)

print(f"RFM9x detected! Version: 0x{version:X}")

# LoRa init (match photon) - buffer, modem, LNA, sync word, 
spi_write(REG_OP_MODE, 0x80) # turn on LoRa
time.sleep(0.01)
set_frequency(868.0)

# set buffers to zero
spi_write(0x0E, 0x00)
spi_write(0x0F, 0x00)

spi_write(0x0C, 0x23) # LNA boost for better reception

# Modem config (matches Photon defaults)
spi_write(0x1D, 0x72) # Bandwidth=125kHz, Coding Rate=4/5
spi_write(0x1E, 0x74) # Spreading factor 7, CRC on
spi_write(0x26, 0x04)

spi_write(0x39, 0x12) # Sync word (like an ID, allows devices to hear each other)

spi_write(0x40, 0x00) # Map DIO0 -> RxDone

spi_write(IRQ_FLAGS, 0xFF) # Reset all notifications

spi_write(REG_OP_MODE, 0x85) # Continuous RX mode

# -- main loop --
receiver = Receiver()
transmitter = Transmitter()
device_ids = []

def check_node_registered(device_id): # Check for new node, register if new
    global deviceIDs
    if device_id not in device_ids:
        print(f"Node registered {device_id} as Device{len(device_ids)}")
        start = time.time()
        while time.time() - start < 5:
            transmitter.format_payload(device_id, 2) # 2 = Acknowledge
        # ADD EXTRA CHECK HERE IF NECESSARY --------------------------------------------------------
        device_ids.append(device_id)

def main_loop():
    while True: # MAIN
        if len(device_ids) > 0:
            for id in device_ids: # Cycle through each node using their ID, receive measurements one node at a timne 
                success = False
                while not success: # Retry if unsuccessful
                    if receiver.receive():
                        device_id, timestamp, temperature, humidity, pressure, soil_moisture, latitude, longitude, flags, crc = receiver.parsePayload()

                        if id == device_id:
                            print("Sending confirmation...")
                            transmitter.format_payload(id, 1) # 1 = Success
                            success = True
                        elif device_id != 00000000:
                            check_node_registered(device_id)
                    else:
                        print("No message received!")
                        transmitter.format_payload(id, 0) # 0 = Fail
                    transmitter.transmit()

                    time.sleep(0.5)
        else:
            if receiver.receive():
                deviceID, *_ = receiver.parse_payload()
                check_node_registered(deviceID)
                transmitter.transmit()

if __name__ == "__main__":
    try:
        main_loop()
    except KeyboardInterrupt:
        print("Exiting...")
    finally:
        spi.close()
        GPIO.cleanup()