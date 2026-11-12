import csv
import tempfile
import unittest
from datetime import datetime, timedelta
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "calibration"))

from calibrate_machine3_database import read_database_samples


def motor_ack(timestamp, gear):
    payload = bytes((3, 6, 0, 4, 0, gear))
    crc = 0xFFFF
    for byte in payload:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    frame = payload + crc.to_bytes(2, "little")
    return f"{timestamp.isoformat(timespec='milliseconds')} RX command MODBUS-RX:[{frame.hex(' ')}]"


class TestMachine3DatabaseCalibration(unittest.TestCase):
    def test_cooling_start_line_keeps_only_cooling_rows_after_line(self):
        start = datetime(2026, 10, 26, 23, 0, 0)
        sample_time = start + timedelta(milliseconds=700)
        csv_fields = (
            "记录时间", "事件来源", "真实温度℃", "真实温度接收时间",
            "NTC1_ADC", "NTC2_ADC", "NTC3_ADC", "NTC接收时间", "模式",
        )
        with tempfile.TemporaryDirectory() as directory:
            csv_path = Path(directory) / "sample.csv"
            serial_path = Path(directory) / "serial.txt"
            with csv_path.open("w", encoding="utf-8-sig", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=csv_fields)
                writer.writeheader()
                writer.writerow({"事件来源": "NTC", "模式": "1"})
                writer.writerow({
                    "事件来源": "真实温度", "真实温度℃": "90",
                    "真实温度接收时间": sample_time.isoformat(),
                })
                writer.writerow({
                    "事件来源": "NTC", "NTC1_ADC": "1874",
                    "NTC2_ADC": "1900", "NTC3_ADC": "2000",
                    "NTC接收时间": sample_time.isoformat(), "模式": "0",
                })
            serial_path.write_text(
                "\n".join((motor_ack(start, 1), motor_ack(
                    start + timedelta(milliseconds=100), 0))) + "\n",
                encoding="utf-8",
            )
            samples, stats = read_database_samples(
                csv_path, serial_path, {"stages": []}, cooling_start_line=4)

        profile = (0, 0, 0.0, 0.0, 1, 3.0, 1.5)
        self.assertEqual(stats["accepted_samples"], 1)
        self.assertEqual([len(samples[profile][channel]) for channel in range(3)],
                         [1, 1, 1])

    def test_samples_require_500ms_since_acknowledged_motor_stop(self):
        start = datetime(2026, 10, 26, 23, 0, 0)
        stopped = start + timedelta(seconds=1)
        csv_fields = (
            "记录时间", "事件来源", "真实温度℃", "真实温度接收时间",
            "NTC1_ADC", "NTC2_ADC", "NTC3_ADC", "NTC接收时间", "模式",
        )
        with tempfile.TemporaryDirectory() as directory:
            csv_path = Path(directory) / "sample.csv"
            serial_path = Path(directory) / "serial.txt"
            with csv_path.open("w", encoding="utf-8-sig", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=csv_fields)
                writer.writeheader()
                for milliseconds in (1499, 1500):
                    stamp = start + timedelta(milliseconds=milliseconds)
                    writer.writerow({
                        "记录时间": stamp.isoformat(),
                        "事件来源": "真实温度",
                        "真实温度℃": "70",
                        "真实温度接收时间": stamp.isoformat(),
                        "模式": "1",
                    })
                    writer.writerow({
                        "记录时间": stamp.isoformat(),
                        "事件来源": "NTC",
                        "NTC1_ADC": "2000",
                        "NTC2_ADC": "2100",
                        "NTC3_ADC": "2200",
                        "NTC接收时间": stamp.isoformat(),
                        "模式": "1",
                    })
            serial_path.write_text(
                "\n".join((motor_ack(start, 1), motor_ack(stopped, 0))) + "\n",
                encoding="utf-8",
            )
            config = {"stages": [{
                "heater_gear": 19,
                "heater_on_seconds": 1,
                "heater_off_seconds": 0,
                "motor_gear": 1,
                "motor_on_seconds": 2,
                "motor_off_seconds": 6,
            }]}

            samples, stats = read_database_samples(
                csv_path, serial_path, config)

        profile = (1, 19, 1.0, 0.0, 1, 2.0, 6.0)
        self.assertEqual(stats["rejected_motor_not_stopped_500ms"], 1)
        self.assertEqual(stats["motor_stopped_500ms"], 1)
        self.assertEqual(stats["accepted_samples"], 1)
        self.assertEqual([len(samples[profile][channel]) for channel in range(3)],
                         [1, 1, 1])


if __name__ == "__main__":
    unittest.main()
