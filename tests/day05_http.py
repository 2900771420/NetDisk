import argparse
import socket

parser = argparse.ArgumentParser()
parser.add_argument("--timeout", type=float, default=5.0)
args = parser.parse_args()

parts = [
    b"GET /health HTTP/1.1\r\n",
    b"Host: 127.0.0.1:8080\r\n",
    b"Connection: close\r\n\r",
    b"\n",
]

with socket.create_connection(("127.0.0.1", 8080), timeout=args.timeout) as sock:
    for part in parts:
        sock.sendall(part)

    response = bytearray()
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        response.extend(chunk)
        if len(response) > 16384:
            raise RuntimeError("response too large for this test")

head, separator, body = bytes(response).partition(b"\r\n\r\n")
if not separator:
    raise RuntimeError("missing response header terminator")

lines = head.split(b"\r\n")
if lines[0] != b"HTTP/1.1 200 OK":
    raise RuntimeError(f"unexpected status: {lines[0]!r}")

headers = {}
for line in lines[1:]:
    name, value = line.split(b":", 1)
    headers[name.lower()] = value.strip()

if headers.get(b"content-length") != str(len(body)).encode("ascii"):
    raise RuntimeError("Content-Length does not match actual body size")
if headers.get(b"connection", b"").lower() != b"close":
    raise RuntimeError("expected Connection: close")
if body != b"OK\n":
    raise RuntimeError(f"unexpected body: {body!r}")

print(f"PASS: HTTP 200, Content-Length={len(body)}, body={body!r}")
