"""Timestamp raw serial lines on arrival, with no decoding: rawts.py <port> <seconds> <out>."""
import sys
import time

import serial

port, seconds, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.05
ser.dtr = False
ser.rts = False
ser.open()
end = time.monotonic() + seconds
buf = b""
with open(out, "w") as f:
    while time.monotonic() < end:
        chunk = ser.read(ser.in_waiting or 1)
        now = time.monotonic()
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            f.write(f"{now:.6f} {line.decode(errors='replace').rstrip()}\n")
    if buf:
        f.write(f"{time.monotonic():.6f} {buf.decode(errors='replace')}\n")
