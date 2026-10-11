#!/usr/bin/env python3
"""Mock harness only: write a serial log the way `idf.py monitor` under `script` would.

    test/fake-serial.py --out serial.log [--boot --host direcsun --ip 192.168.1.77]
                        [--seconds N] [--loop-us 900:940] [--spike S:US] [--reboot-at S]
                        [--baro-off-from S] [--wifi-drop-at S]

Line formats are copied from the R3.5 firmware's format strings
(direcsun_comms/src/wifi.cpp, ws_server.cpp; main/services.cpp) and the ESP-IDF
log prefix, colour codes and CRLF included. With --seconds it appends one status
pair per second in real time, so a reader tailing the file sees it grow. These
lines are a fixture, not evidence about the box.
"""
import argparse
import random
import time

GREEN, YELLOW, RESET = "\x1b[0;32m", "\x1b[0;33m", "\x1b[0m"


def line(f, level, ms, tag, text):
    colour = GREEN if level == "I" else YELLOW
    f.write(f"{colour}{level} ({ms}) {tag}: {text}{RESET}\r\n")


def boot(f, host, ip):
    f.write("ESP-ROM:esp32s3-20210327\r\nBuild:Mar 27 2021\r\nrst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n")
    line(f, "I", 1612, "wifi", 'joining "BenchNet"')
    f.write(f"{GREEN}I (1615) wifi:mode : sta (e0:72:a1:fc:49:5c){RESET}\r\n")   # the IDF driver's own tag
    line(f, "I", 1630, "wifi", f"mDNS hostname {host}.local")
    line(f, "I", 1640, "ws", "WebSocket server on port 80 at /ws (up to 3 clients)")
    line(f, "I", 3890, "wifi", f"connected, address {ip}")
    line(f, "I", 3890, "wifi", f"WebSocket: ws://{host}.local/ws  or  ws://{ip}/ws")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--boot", action="store_true")
    parser.add_argument("--host", default="direcsun")
    parser.add_argument("--ip", default="192.168.1.77")
    parser.add_argument("--seconds", type=int, default=0)
    parser.add_argument("--loop-us", default="900:940")
    parser.add_argument("--spike", help="S:US, loop_us_max US at second S")
    parser.add_argument("--reboot-at", type=int)
    parser.add_argument("--baro-off-from", type=int)
    parser.add_argument("--wifi-drop-at", type=int)
    args = parser.parse_args()
    lo, hi = map(int, args.loop_us.split(":"))
    spike = tuple(map(int, args.spike.split(":"))) if args.spike else None
    with open(args.out, "a", buffering=1) as f:
        if args.boot:
            boot(f, args.host, args.ip)
        s, ms, sent = 0, 4000, 0
        for i in range(1, args.seconds + 1):
            if args.reboot_at == i:
                boot(f, args.host, args.ip)
                s, ms = 0, 4000
            s += 1
            ms += 1000
            sent += 500
            loop = spike[1] if spike and spike[0] == i else random.randint(lo, hi)
            line(f, "I", ms, "status", f"s {s} ticks 500 missed 0 (total 0) loop_us_max {loop} dev_max_us 2 "
                                      f"p99_dev_us 0 dshot sent {sent} dropped 0 imu_invalid 0")
            baro = "--" if args.baro_off_from and i >= args.baro_off_from else "ok"
            line(f, "I", ms, "status", "mode SAFE throttle 0 vbat 10.02 rate 0.01 bias -2.08 tilt 4.0 psi -0.0 "
                                      f"wheel 0 latched 0x0000 flags 0x00 baro {baro}")
            if args.wifi_drop_at == i:
                line(f, "W", ms, "wifi", "disconnected (reason 8), retrying in 1 s")
            f.flush()
            time.sleep(1.0)


if __name__ == "__main__":
    main()
