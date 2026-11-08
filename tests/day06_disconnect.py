import http.client
import socket
import struct

HOST = "127.0.0.1"
PORT = 8080

def health():
    conn = http.client.HTTPConnection(HOST, PORT, timeout=5)
    try:
        conn.request("GET", "/health")
        response = conn.getresponse()
        body = response.read()
        if response.status != 200 or body != b"OK\n":
            raise RuntimeError("health response mismatch")
        if response.getheader("Content-Length") != str(len(body)):
            raise RuntimeError("Content-Length mismatch")
    finally:
        conn.close()

health()

with socket.create_connection((HOST, PORT), timeout=5) as sock:
    sock.sendall(b"GET /health HTTP/1.1\r\nHost:")
health()

with socket.create_connection((HOST, PORT), timeout=5) as sock:
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                    struct.pack("ii", 1, 0))
    sock.sendall(b"GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n")
health()

for _ in range(10):
    health()

print("PASS: incomplete request, reset attempt and later health requests")
