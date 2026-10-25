"""Capture a small, allowlisted thermal/battery trace from an explicitly named device.

Does not change charging, thermal policy, app state, brightness or log buffers.
Raw dumpsys output (which can contain unrelated device information) is never saved.
"""
import argparse
import datetime as dt
import json
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--duration", type=float, default=240)
    parser.add_argument("--interval", type=float, default=5)
    parser.add_argument("--stop-file", type=Path)
    args = parser.parse_args()
    if not 1 <= args.duration <= 600 or not 2 <= args.interval <= 30:
        parser.error("Use duration 1..600 seconds and interval 2..30 seconds")

    def shell(*command):
        result = subprocess.run([args.adb, "-s", args.serial, "shell", *command],
                                capture_output=True, text=True, timeout=8)
        if result.returncode:
            raise RuntimeError("ADB command failed")
        return result.stdout

    # No implicit device selection, connect attempt or waiting indefinitely for ADB.
    state = subprocess.run([args.adb, "-s", args.serial, "get-state"],
                           capture_output=True, text=True, timeout=8)
    if state.returncode or state.stdout.strip() != "device":
        raise SystemExit("Named ADB device is unavailable")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    failures = 0
    with args.output.open("x", encoding="utf-8") as stream:
        while time.monotonic() - started < args.duration:
            if args.stop_file and args.stop_file.exists():
                break
            cycle = time.monotonic()
            sample = {"event": "thermal", "utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                      "host_elapsed_s": cycle - started}
            try:
                sample["device_elapsed_s"] = float(shell("cat", "/proc/uptime").split()[0])
                thermal = shell("dumpsys", "thermalservice")
                status = re.search(r"Thermal Status:\s*(\d+)", thermal)
                sample["thermal_status"] = int(status[1]) if status else None
                current = re.search(r"Current temperatures from HAL:\s*([\s\S]*?)Current cooling devices", thermal)
                sensors = {}
                for value, name, sensor_status in re.findall(
                        r"Temperature\{mValue=([-\d.]+), mType=\d+, mName=(AP|BAT|SKIN|USB), mStatus=(\d+)\}",
                        current[1] if current else ""):
                    sensors[name] = {"celsius": float(value), "status": int(sensor_status)}
                sample["sensors"] = sensors
                battery = shell("dumpsys", "battery")
                fields = {}
                for name, value in re.findall(
                        r"^  (AC powered|USB powered|Wireless powered|status|level|temperature):\s*(\S+)", battery, re.M):
                    fields[name] = value == "true" if value in ("true", "false") else int(value)
                sample["battery"] = fields
                saver = shell("settings", "get", "global", "low_power").strip()
                sample["power_saver"] = {"0": False, "1": True}.get(saver)
                failures = 0
            except (RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
                sample["event"] = "capture_error"
                sample["reason"] = type(error).__name__
                failures += 1
            stream.write(json.dumps(sample, allow_nan=False) + "\n")
            stream.flush()
            if failures >= 3:
                break
            time.sleep(max(0, args.interval - (time.monotonic() - cycle)))
    print(json.dumps({"output": str(args.output.resolve()), "capture_errors_at_end": failures}))


if __name__ == "__main__":
    main()
