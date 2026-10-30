# Crash forensics: continuously append the device's raw serial output to a log file.
# Usage: python tools/serial_dump.py COM10 crash.log
# Auto-reconnect when the port drops because the device rebooted (COM number unchanged).
import sys
import time

import serial


def main() -> None:
    if len(sys.argv) < 3:
        print("usage: python tools/serial_dump.py PORT LOGFILE")
        sys.exit(1)
    port, logfile = sys.argv[1], sys.argv[2]
    while True:
        try:
            with serial.Serial(port, 115200, timeout=1) as ser:
                print(f"[serial_dump] listening {port} -> {logfile}", flush=True)
                with open(logfile, "ab") as f:
                    while True:
                        data = ser.read(4096)
                        if data:
                            f.write(data)
                            f.flush()
        except serial.SerialException as e:
            # port disappeared (device crashed/rebooted or re-enumerated), retry in 2 seconds
            print(f"[serial_dump] port lost ({e}), retrying in 2s...", flush=True)
            time.sleep(2)
        except KeyboardInterrupt:
            print("[serial_dump] bye", flush=True)
            return


if __name__ == "__main__":
    main()
