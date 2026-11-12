import unittest
import tempfile
import struct
from pathlib import Path
from telemetry import (
    crc16, request_ntcs, request_station3_snapshot, parse_ntcs,
    parse_ntc_temperatures, parse_station3_motor_rpm, lookup, Recorder,
    request_station1_board_temperature, parse_station1_board_temperature, TemperatureErrorStats,
)


class TestTelemetry(unittest.TestCase):
    def test_error_stats_absolute_inclusive_and_invalid(self):
        stats = TemperatureErrorStats()
        self.assertIn('覆盖率：--', stats.summary())
        for error in (None, float('nan'), float('inf'), -float('inf')):
            stats.add(error, 80)
        self.assertEqual(stats.count, 0)
        for error in (-3, -2, -1, 0, 1, 2):
            stats.add(error, 80)
        self.assertEqual((stats.count, stats.maximum, stats.within_one, stats.within_two),
                         (6, 3, 3, 5))
        self.assertIn('最大绝对温差：3.00 ℃', stats.summary())
        self.assertIn('≤1℃覆盖率：50.0%', stats.summary())
        self.assertIn('≤2℃覆盖率：83.3%', stats.summary())

    def test_maximum_only_uses_reference_at_least_80(self):
        stats = TemperatureErrorStats()
        for reference in (None, float('nan'), float('inf'), 79.9):
            stats.add(20, reference)
        self.assertIn('最大绝对温差：--', stats.summary())
        stats.add(-2, 80)
        stats.add(1, 80)
        stats.add(30, 59)
        self.assertEqual(stats.maximum, 2)
        self.assertEqual(stats.maximum_count, 2)
        self.assertEqual(stats.count, 7)

    def test_three_degree_coverage_and_last_over_two(self):
        stats = TemperatureErrorStats()
        for error in (2, -2, 3, -3, 3.1):
            stats.add(error, 85, 85 + error, '2026-11-04T15:00:00')
        self.assertEqual(stats.within_three, 4)
        self.assertIn('≤3℃覆盖率：80.0%', stats.summary())
        self.assertEqual(stats.last_over_two[1:], (85, 88.1, 3.1))
        stats.add(-2.5, 70, 67.5, '2026-11-04T15:01:00')
        self.assertEqual(stats.last_over_two, ('2026-11-04T15:01:00', 70, 67.5, -2.5))
        saved = stats.last_over_two
        stats.add(2, 85, 87)
        stats.add(None, 85, 87)
        stats.add(5, None, None)
        self.assertEqual(stats.last_over_two, saved)
        self.assertIn('真实温度：70.00 ℃', stats.summary())
        self.assertIn('板级温度：67.50 ℃', stats.summary())
        self.assertIsNone(TemperatureErrorStats().last_over_two)

    def test_request_and_response(self):
        request=bytes.fromhex(request_ntcs().split('[')[1].split(']')[0])
        self.assertEqual(crc16(request),0)
        data=bytearray(138)
        for address,value in zip((0,65,67),(1234,2345,3456)):
            data[address*2:address*2+4]=value.to_bytes(4,'big')
        response=bytes((2,4,138))+data
        response+=crc16(response).to_bytes(2,'little')
        line='MODBUS-RX:['+response.hex(' ')+']'
        self.assertEqual(parse_ntcs(line),(1234,2345,3456))
        bad=bytearray(response);bad[-1]^=1
        self.assertIsNone(parse_ntcs('MODBUS-RX:['+bad.hex(' ')+']'))

    def test_parse_firmware_ntc_temperatures(self):
        line = ('I (2653717) ntcTemp: NTC temperatures: '
                'NTC1=52.33 C, NTC2=47.28 C, NTC3=51.77 C\x1b[0m')
        self.assertEqual(parse_ntc_temperatures(line), (52.33, 47.28, 51.77))
        self.assertIsNone(parse_ntc_temperatures(
            'ntcTemp: NTC temperatures: NTC1=52.33 C, NTC2=47.28 C'))
        self.assertIsNone(parse_ntc_temperatures(
            'ntcTemp: NTC temperatures: NTC1=52.33 C, NTC2=47.28 C, NTC3=999 C'))

    def test_high_temperature_normalization(self):
        points = [(0, 97.8), (10, 98.1), (20, 98.5)]
        self.assertEqual(lookup(points, 5, capped=True), 98.0)
        self.assertEqual(lookup(points, 10, capped=True), 98.1)
        self.assertEqual(lookup(points, 20, capped=True), 98.5)

    def test_station3_snapshot_rpm_request_and_parser(self):
        self.assertEqual(
            request_station3_snapshot(),
            'MODBUS-TX:[03 04 00 00 00 18 F1 E2]\r\n')
        data = bytearray(48)
        data[0:2] = (1450).to_bytes(2, 'big')
        frame = bytes((3, 4, len(data))) + data
        frame += crc16(frame).to_bytes(2, 'little')
        line = 'MODBUS-RX:[' + frame.hex(' ') + ']'
        self.assertEqual(parse_station3_motor_rpm(line), 1450)
        bad = bytearray(frame)
        bad[-1] ^= 1
        self.assertIsNone(parse_station3_motor_rpm(
            'MODBUS-RX:[' + bad.hex(' ') + ']'))
        self.assertIsNone(parse_station3_motor_rpm(
            'MODBUS-RX:[03 04 02 05 AA]'))

    def test_station1_board_temperature_request_and_parser(self):
        self.assertEqual(
            request_station1_board_temperature(),
            'MODBUS-TX:[01 04 00 0C 00 02 B1 C8]\r\n')
        request = bytes.fromhex(
            request_station1_board_temperature().split('[')[1].split(']')[0])
        self.assertEqual(request[:6], bytes.fromhex('01 04 00 0C 00 02'))
        self.assertEqual(crc16(request), 0)

        response = bytes((1, 4, 4)) + struct.pack('>f', 85.25)
        response += crc16(response).to_bytes(2, 'little')
        line = 'MODBUS-RX:[' + response.hex(' ') + ']'
        self.assertEqual(parse_station1_board_temperature(line), 85.25)

        bad = bytearray(response)
        bad[-1] ^= 1
        self.assertIsNone(parse_station1_board_temperature(
            'MODBUS-RX:[' + bad.hex(' ') + ']'))
        invalid = bytes((1, 4, 4)) + struct.pack('>f', float('nan'))
        invalid += crc16(invalid).to_bytes(2, 'little')
        self.assertIsNone(parse_station1_board_temperature(
            'MODBUS-RX:[' + invalid.hex(' ') + ']'))

    def test_recording(self):
        with tempfile.TemporaryDirectory() as directory:
            r=Recorder(directory)
            r.raw('RX T=60');r.sample('NTC',60,'ref-time',(2000,2100,2200),'adc-time',2)
            r.sample('NTC查表温度',60,'ref-time',(2000,2100,2200),'adc-time',2,
                     (52.33,47.28,51.77),'temp-time',1450,'rpm-time',
                     59.5,'board-time')
            r.sample('真实温度',61,'ref-time2',None,'',2);r.close()
            text=r.csv_path.read_text(encoding='utf-8-sig')
            self.assertIn('2000,2100,2200',text)
            self.assertIn('NTC2本地拟合',text)
            self.assertIn('NTC3查表温度℃',text)
            self.assertIn('52.33,47.28,51.77',text)
            self.assertIn('-7.67,-12.72,-8.23',text)
            self.assertIn('站3电机转速RPM',text)
            self.assertIn('1450,rpm-time',text)
            self.assertIn('ESP32板级温度℃',text)
            self.assertIn('59.5,board-time,-0.5',text)
            self.assertIn('RX T=60',r.raw_path.read_text(encoding='utf-8'))
            self.assertIn('工艺1', text)


if __name__=='__main__':unittest.main()
