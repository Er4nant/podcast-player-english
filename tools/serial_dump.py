# 崩溃取证用：把设备串口原始输出持续追加写入日志文件。
# 用法: python tools/serial_dump.py COM10 crash.log
# 设备重启导致端口断开时自动重连（COM 号不变的情况）。
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
            # 端口消失（设备崩溃重启/重新枚举），2 秒后重试
            print(f"[serial_dump] port lost ({e}), retrying in 2s...", flush=True)
            time.sleep(2)
        except KeyboardInterrupt:
            print("[serial_dump] bye", flush=True)
            return


if __name__ == "__main__":
    main()
