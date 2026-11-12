#!/usr/bin/env python3
"""Serial monitor that keeps running across flashing and USB reconnects.

Run it in a second terminal window and leave it open:

    scripts/monitor.py                 # /dev/ttyUSB0, 115200 Bd
    scripts/monitor.py -p /dev/ttyACM0

While scripts/idf.sh flashes the board it creates a pause flag; the monitor then
releases the port, waits, reconnects and resets the board so the whole boot log
is visible. Everything is also appended to logs/serial.log.

For decoding panic backtraces use `scripts/idf.sh -p PORT monitor` instead.
"""
import argparse
import datetime
import os
import sys
import time

import serial

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIM, RESET = "\033[2m", "\033[0m"


def pause_flag(port: str) -> str:
    runtime = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
    return os.path.join(runtime, f"esp-serial-{os.path.basename(port)}.pause")


def banner(text: str, log) -> None:
    stamp = datetime.datetime.now().strftime("%H:%M:%S")
    line = f"--- {stamp} {text} ---"
    sys.stdout.write(f"\n{DIM}{line}{RESET}\n")
    sys.stdout.flush()
    log.write(f"\n{line}\n".encode())
    log.flush()


def reset_board(ser: serial.Serial) -> None:
    """Pulse EN via RTS, like esptool's hard reset (DTR stays released)."""
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-p", "--port", default="/dev/ttyUSB0")
    parser.add_argument("-b", "--baud", type=int, default=115200)
    parser.add_argument("--reset", action="store_true", help="reset the board on start")
    parser.add_argument("--log", default=os.path.join(ROOT, "logs", "serial.log"))
    args = parser.parse_args()

    os.makedirs(os.path.dirname(args.log), exist_ok=True)
    flag = pause_flag(args.port)
    ser = None
    reset_next = args.reset
    state = None  # last reported state, to print each banner once

    with open(args.log, "ab") as log:
        banner(f"monitor {args.port} @ {args.baud}, Ctrl+C to quit", log)
        try:
            while True:
                if os.path.exists(flag):
                    if ser is not None:
                        ser.close()
                        ser = None
                    if state != "flashing":
                        banner("flashing, port released", log)
                        state = "flashing"
                    reset_next = True
                    time.sleep(0.1)
                    continue

                if ser is None:
                    try:
                        ser = serial.Serial(args.port, args.baud, timeout=0.1)
                    except (serial.SerialException, OSError):
                        if state != "waiting":
                            banner(f"waiting for {args.port}", log)
                            state = "waiting"
                        time.sleep(0.5)
                        continue
                    if reset_next:
                        reset_board(ser)
                        reset_next = False
                    banner("connected", log)
                    state = "connected"

                try:
                    data = ser.read(4096)
                except (serial.SerialException, OSError):
                    ser.close()
                    ser = None
                    banner("disconnected", log)
                    state = "disconnected"
                    continue
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.flush()
                    log.write(data)
                    log.flush()
        except KeyboardInterrupt:
            banner("monitor stopped", log)
        finally:
            if ser is not None:
                ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
