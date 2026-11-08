import argparse
import socket

parser = argparse.ArgumentParser()
parser.add_argument("--timeout", type=float, default=5.0)
args = parser.parse_args()

payload = b"hello\x00disk\n"

with socket.create_connection(("127.0.0.1", 8080), timeout=args.timeout) as sock:
    sock.sendall(payload)
    sock.shutdown(socket.SHUT_WR)

    received = bytearray()
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        received.extend(chunk)

if received != payload:
    raise RuntimeError(f"echo mismatch: expected {payload!r}, got {bytes(received)!r}")

print(f"PASS: {len(received)} bytes echoed exactly")
