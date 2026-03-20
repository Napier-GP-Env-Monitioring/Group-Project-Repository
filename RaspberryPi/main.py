from threading import Thread
from app import app
import socket

UDP_PORT = 540

def udpListener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', UDP_PORT))
    print(f"UDP listener running on port {UDP_PORT}")
    while True:
        data, addr = sock.recvfrom(1024)
        print("Received UDP:", data)

if __name__ == "__main__":
    # start UDP listener in background thread
    Thread(target=udpListener, daemon=True).start()

    # start Flask server (blocks main thread)
    app.run(host="0.0.0.0", port=5000)