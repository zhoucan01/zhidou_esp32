"""Stream NTC2 ADC values from ESP32 MODBUS-TX/MODBUS-RX text logs."""

import argparse
import csv
import datetime
import re
import sys
from collections import defaultdict, deque
from pathlib import Path


FRAME_PATTERN = re.compile(rb"MODBUS-(TX|RX):\s*\[([0-9A-Fa-f \t]+)\]", re.IGNORECASE)
TIME_PATTERN = re.compile(rb"\[(\d{2}:\d{2}:\d{2}(?:\.\d+)?)\]")
LINE_TIME_PATTERN = re.compile(rb"^(\d{2}:\d{2}:\d{2}(?:\.\d+)?)\s")
DATE_PATTERN = re.compile(rb"(20\d{2}-\d{2}-\d{2})")
NTC2_REGISTER = 0x0041
NTC2_CACHE_REGISTER = 0x0141
POWER_CACHE_REGISTER = 0x0200


def has_valid_crc(frame):
    if len(frame) < 4:
        return False
    crc = 0xFFFF
    for value in frame[:-2]:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return frame[-2:] == bytes((crc & 0xFF, crc >> 8))


def parse_hex_frame(payload):
    tokens = payload.split()
    if not tokens or any(len(token) != 2 for token in tokens):
        return None
    try:
        return bytes(int(token, 16) for token in tokens)
    except ValueError:
        return None


def parse_read_request(frame):
    if len(frame) != 8 or not has_valid_crc(frame) or frame[1] != 0x04:
        return None
    start = int.from_bytes(frame[2:4], "big")
    count = int.from_bytes(frame[4:6], "big")
    if count == 0:
        return None
    return frame[0], start, count


def extract_adc(request, response):
    """Return source, two registers and uint32 ADC, or None if unrelated."""
    if request is None or len(response) < 5 or not has_valid_crc(response):
        return None
    unit, function, byte_count = response[:3]
    request_unit, start, requested_count = request
    if unit != request_unit or function != 0x04:
        return None
    data = response[3:-2]
    if byte_count != len(data) or byte_count % 2 or byte_count != requested_count * 2:
        return None

    if unit == 0x02:
        source = "station2"
        target = NTC2_REGISTER
    elif unit == 0x01:
        source = "station1_cache"
        target = NTC2_CACHE_REGISTER
    else:
        return None
    if start > target or start + requested_count < target + 2:
        return None

    index = (target - start) * 2
    high = int.from_bytes(data[index:index + 2], "big")
    low = int.from_bytes(data[index + 2:index + 4], "big")
    return source, high, low, (high << 16) | low


def extract_power_state(request, response):
    if request is None or len(response) < 5 or not has_valid_crc(response):
        return None
    unit, function, byte_count = response[:3]
    request_unit, start, requested_count = request
    if unit != 0x01 or request_unit != unit or function != 0x04:
        return None
    data = response[3:-2]
    if byte_count != len(data) or byte_count != requested_count * 2:
        return None
    if start > POWER_CACHE_REGISTER or start + requested_count < POWER_CACHE_REGISTER + 8:
        return None

    offset = (POWER_CACHE_REGISTER - start) * 2
    registers = [int.from_bytes(data[offset + i * 2:offset + i * 2 + 2], "big")
                 for i in range(8)]
    power_enabled, heater1_enabled, heater2_enabled = registers[3:6]
    heater1_gear, heater2_gear = registers[6:8]
    effective_gear = 0
    if power_enabled:
        if heater1_enabled:
            effective_gear = max(effective_gear, heater1_gear)
        if heater2_enabled:
            effective_gear = max(effective_gear, heater2_gear)
    return (power_enabled, heater1_enabled, heater2_enabled,
            heater1_gear, heater2_gear, effective_gear)


def process_log(lines, writer):
    pending = defaultdict(deque)
    stats = {"tx": 0, "rx": 0, "bad_crc": 0, "matched": 0}
    current_date = None
    previous_clock = None
    power_state = None

    for line_number, line in enumerate(lines, 1):
        date_match = DATE_PATTERN.search(line)
        if current_date is None and date_match:
            current_date = datetime.date.fromisoformat(date_match.group(1).decode("ascii"))
        timestamp_match = LINE_TIME_PATTERN.search(line) or TIME_PATTERN.search(line)
        timestamp = ""
        if timestamp_match:
            clock = timestamp_match.group(1).decode("ascii")
            clock_value = datetime.time.fromisoformat(clock)
            if (previous_clock is not None and current_date is not None and
                    previous_clock.hour >= 12 and clock_value.hour < 12):
                current_date += datetime.timedelta(days=1)
            previous_clock = clock_value
            timestamp = f"{current_date.isoformat()} {clock}" if current_date else clock
        for direction, payload in FRAME_PATTERN.findall(line):
            frame = parse_hex_frame(payload)
            if frame is None:
                continue
            if direction.upper() == b"TX":
                stats["tx"] += 1
                request = parse_read_request(frame)
                if request is not None:
                    pending[(request[0], 0x04)].append(request)
                continue

            stats["rx"] += 1
            if not has_valid_crc(frame):
                stats["bad_crc"] += 1
                continue
            if len(frame) < 2:
                continue
            key = (frame[0], frame[1] & 0x7F)
            if not pending[key]:
                continue
            request = pending[key].popleft()
            result = extract_adc(request, frame)
            if result is not None:
                source, high, low, adc = result
                power = power_state or ("", "", "", "", "", "")
                writer.writerow((line_number, timestamp, source, request[1], request[2],
                                 high, low, adc, *power))
                stats["matched"] += 1
                continue
            power_state = extract_power_state(request, frame) or power_state
    return stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="large UART0 text log; read incrementally")
    parser.add_argument("-o", "--output", type=Path,
                        help="CSV output path (default: next to the log, "
                             "<log-dir>/<log-stem>_ntc2_adc.csv)")
    args = parser.parse_args()
    # Raw captures live under testdata/ntcdata/<YYMMDD>/, so the derived CSV
    # sits beside the log by default.
    default_output = args.log.with_name(args.log.stem + "_ntc2_adc.csv")
    output = args.output or default_output

    with args.log.open("rb") as source, output.open("w", encoding="utf-8", newline="") as destination:
        writer = csv.writer(destination)
        writer.writerow(("rx_line", "timestamp", "source", "request_start", "request_count",
                         "register_0041_or_0141", "register_0042_or_0142", "ntc2_adc",
                         "power_enabled", "heater1_enabled", "heater2_enabled",
                         "heater1_gear", "heater2_gear", "effective_gear"))
        stats = process_log(source, writer)

    print(f"Wrote {stats['matched']} NTC2 samples to {output}", file=sys.stderr)
    print(f"TX={stats['tx']} RX={stats['rx']} bad_CRC={stats['bad_crc']}", file=sys.stderr)


if __name__ == "__main__":
    main()