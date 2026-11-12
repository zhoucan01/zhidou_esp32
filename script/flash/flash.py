#!/usr/bin/env python3
"""Flash existing ESP-IDF binaries directly through Espressif esptool."""
import argparse
import importlib.metadata
import json
from pathlib import Path
import subprocess
import sys
import time


def detect_port(chip, before, spelling):
    try:
        from serial.tools import list_ports
    except ImportError:
        raise RuntimeError("pyserial is missing; install it with esptool") from None

    ports = []
    for attempt in range(10):
        ports = list(list_ports.comports())
        if ports:
            break
        if attempt < 9:
            print("Waiting for a serial port...", flush=True)
            time.sleep(1)
    if not ports:
        raise RuntimeError("No serial ports found; connect the ESP32-S3 and check its USB driver")

    for port in reversed(ports):
        print(f"Probing serial port: {port.device}", flush=True)
        command = [sys.executable, "-m", "esptool", "--chip", chip,
                   "--port", port.device, "--before", spelling(before),
                   "--after", spelling("no_reset"), spelling("chip_id")]
        try:
            result = subprocess.run(command, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL, timeout=4)
        except (OSError, subprocess.TimeoutExpired):
            continue
        if result.returncode == 0:
            time.sleep(1)
            return port.device
    raise RuntimeError("No ESP32-S3 device responded on the available serial ports")


def main():
    here = Path(__file__).resolve().parent
    default_dir = here if (here / "flasher_args.json").is_file() else here.parent.parent / "build"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Serial port, e.g. COM4; auto-detect when omitted")
    parser.add_argument("--baud", type=int, default=460800, help="Default: 460800; try 115200 if unstable")
    parser.add_argument("--firmware-dir", type=Path, default=default_dir,
                        help="Directory containing flasher_args.json and binaries")
    parser.add_argument("--dry-run", action="store_true", help="Validate files and print command without opening the port")
    args = parser.parse_args()
    if args.baud <= 0:
        parser.error("--baud must be positive")

    directory = args.firmware_dir.resolve()
    manifest = json.loads((directory / "flasher_args.json").read_text(encoding="utf-8"))
    extra = manifest["extra_esptool_args"]
    if extra["chip"] != "esp32s3":
        raise ValueError("Expected an ESP32-S3 firmware bundle")
    if not manifest["flash_files"]:
        raise ValueError("Firmware manifest contains no binaries")
    binaries = []
    for offset, filename in manifest["flash_files"].items():
        int(offset, 0)
        binary = (directory / filename).resolve()
        if not binary.is_file() or binary.stat().st_size == 0:
            raise ValueError(f"Missing or empty binary: {binary}")
        binaries.extend([offset, str(binary)])

    try:
        major = int(importlib.metadata.version("esptool").split(".")[0])
    except importlib.metadata.PackageNotFoundError:
        raise RuntimeError('esptool is missing. Run: python -m pip install "esptool>=4.8,<6"') from None
    if major not in (4, 5):
        raise RuntimeError("This script supports esptool 4.x and 5.x")

    def spelling(value):
        return value.replace("_", "-") if major >= 5 else value

    port = args.port
    if not port:
        if args.dry_run:
            from serial.tools import list_ports
            ports = list(list_ports.comports())
            if not ports:
                raise RuntimeError("No serial ports found; connect the ESP32-S3 and check its USB driver")
            port = ports[-1].device
        else:
            port = detect_port(extra["chip"], extra["before"], spelling)
        print(f"Auto-detected serial port: {port}", flush=True)

    base = [sys.executable, "-m", "esptool", "--chip", extra["chip"],
            "--port", port, "--baud", str(args.baud),
            "--before", spelling(extra["before"]), "--after", spelling("no_reset")]
    if not extra.get("stub", True):
        base.append("--no-stub")
    # The whole flash is erased before every download so stale OTA data or
    # leftovers from an older layout cannot survive the update.
    erase_command = base + [spelling("erase_flash")]
    print("Erase command: " + subprocess.list2cmdline(erase_command), flush=True)
    command = base + [spelling("write_flash")]
    command.extend(spelling(item) if item.startswith("--") else item
                   for item in manifest["write_flash_args"])
    command.extend(binaries)
    print("Command: " + subprocess.list2cmdline(command), flush=True)

    if not args.dry_run:
        subprocess.run(erase_command, cwd=directory, check=True)
        subprocess.run(command, cwd=directory, check=True)
        reset_command = [sys.executable, "-m", "esptool", "--chip", extra["chip"],
                         "--port", port, "--before", spelling("no_reset"),
                         "--after", spelling("hard_reset"), spelling("run")]
        print("Reset command: " + subprocess.list2cmdline(reset_command), flush=True)
        try:
            subprocess.run(reset_command, cwd=directory, check=True)
        except subprocess.CalledProcessError as error:
            raise RuntimeError(
                f"Flash succeeded, but automatic reset failed with exit code {error.returncode}; press EN/RESET manually"
            ) from error
        print("Flash succeeded.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Flash failed: {error}", file=sys.stderr)
        sys.exit(1)