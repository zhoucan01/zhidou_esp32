"""Station 2 input-register polling and durable session recording."""
import csv
import math
import re
import struct
from pathlib import Path
from datetime import datetime

NTC_TEMPERATURES = re.compile(
    r"ntcTemp:\s*NTC temperatures:\s*"
    r"NTC1\s*=\s*(-?\d+(?:\.\d+)?)\s*C\s*,\s*"
    r"NTC2\s*=\s*(-?\d+(?:\.\d+)?)\s*C\s*,\s*"
    r"NTC3\s*=\s*(-?\d+(?:\.\d+)?)\s*C",
    re.I,
)
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
STATION3_SLAVE = 0x03
STATION3_SNAPSHOT_REGISTERS = 0x18
STATION1_SLAVE = 0x01
STATION1_BOARD_TEMPERATURE_REGISTER = 0x000C


class TemperatureErrorStats:
    """Session-wide, equally weighted valid temperature comparison samples."""

    def __init__(self):
        self.count = 0
        self.maximum = 0.0
        self.maximum_count = 0
        self.within_one = 0
        self.within_two = 0
        self.within_three = 0
        self.last_over_two = None

    def add(self, error, reference_temperature=None, board_temperature=None, recorded_at=None):
        if error is None or not math.isfinite(error):
            return
        magnitude = abs(error)
        self.count += 1
        if (reference_temperature is not None and
                math.isfinite(reference_temperature) and reference_temperature >= 80):
            self.maximum = max(self.maximum, magnitude)
            self.maximum_count += 1
        self.within_one += magnitude <= 1.0
        self.within_two += magnitude <= 2.0
        self.within_three += magnitude <= 3.0
        if (magnitude > 2.0 and reference_temperature is not None and
                board_temperature is not None and math.isfinite(reference_temperature) and
                math.isfinite(board_temperature)):
            self.last_over_two = (recorded_at or datetime.now().isoformat(timespec='seconds'),
                                  reference_temperature, board_temperature, error)

    def summary(self):
        if not self.count:
            return ('最大绝对温差：-- ℃（真实温度≥80℃）    ≤1℃覆盖率：--    '
                    '≤2℃覆盖率：--    ≤3℃覆盖率：--    有效样本：0\n最近一次|温差|>2℃：--')
        maximum = f'{self.maximum:.2f}' if self.maximum_count else '--'
        latest = '最近一次|温差|>2℃：--'
        if self.last_over_two is not None:
            when, real, board, error = self.last_over_two
            latest = (f'最近一次|温差|>2℃：{when}    真实温度：{real:.2f} ℃    '
                      f'板级温度：{board:.2f} ℃    温差：{error:+.2f} ℃')
        return (f'最大绝对温差：{maximum} ℃（真实温度≥80℃）    '
                f'≤1℃覆盖率：{self.within_one / self.count:.1%}    '
                f'≤2℃覆盖率：{self.within_two / self.count:.1%}    '
                f'≤3℃覆盖率：{self.within_three / self.count:.1%}    '
                f'有效样本：{self.count}\n{latest}')


def lookup(points, adc, capped=False):
    if adc <= points[0][0]: value = points[0][1]
    elif adc >= points[-1][0]: value = points[-1][1]
    else:
        for (a, t), (b, u) in zip(points, points[1:]):
            if a <= adc <= b:
                value = t + (u-t)*(adc-a)/(b-a)
                break
    return 98.0 if capped and 97.9 <= value < 98.0 else round(value, 3)


def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte
        for _ in range(8): crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return crc


def request_ntcs():
    data = bytes((2, 4, 0, 0, 0, 69))
    frame = data + crc16(data).to_bytes(2, 'little')
    return 'MODBUS-TX:[' + frame.hex(' ').upper() + ']\r\n'


def request_station3_snapshot():
    data = bytes((STATION3_SLAVE, 4, 0, 0, 0,
                  STATION3_SNAPSHOT_REGISTERS))
    frame = data + crc16(data).to_bytes(2, 'little')
    return 'MODBUS-TX:[' + frame.hex(' ').upper() + ']\r\n'


def request_station1_board_temperature():
    data = bytes((STATION1_SLAVE, 4,
                  STATION1_BOARD_TEMPERATURE_REGISTER >> 8,
                  STATION1_BOARD_TEMPERATURE_REGISTER & 0xff, 0, 2))
    frame = data + crc16(data).to_bytes(2, 'little')
    return 'MODBUS-TX:[' + frame.hex(' ').upper() + ']\r\n'


def parse_ntcs(line):
    match = re.search(r'MODBUS-RX:\[([0-9A-Fa-f\s]+)\]', line)
    if not match: return None
    try: frame = bytes.fromhex(match[1])
    except ValueError: return None
    if len(frame) != 143 or frame[:3] != bytes((2, 4, 138)): return None
    if crc16(frame[:-2]) != int.from_bytes(frame[-2:], 'little'): return None
    data = frame[3:-2]
    return tuple(int.from_bytes(data[address*2:address*2+4], 'big') for address in (0, 65, 67))


def parse_station1_board_temperature(line):
    match = re.search(r'MODBUS-RX:\[([0-9A-Fa-f\s]+)\]', line)
    if not match:
        return None
    try:
        frame = bytes.fromhex(match[1])
    except ValueError:
        return None
    if (len(frame) != 9 or frame[:3] != bytes((STATION1_SLAVE, 4, 4)) or
            crc16(frame[:-2]) != int.from_bytes(frame[-2:], 'little')):
        return None
    temperature = struct.unpack('>f', frame[3:7])[0]
    if not math.isfinite(temperature) or not -40 <= temperature <= 200:
        return None
    return round(temperature, 3)


def parse_station3_motor_rpm(line):
    match = re.search(r'MODBUS-RX:\[([0-9A-Fa-f\s]+)\]', line)
    if not match:
        return None
    try:
        frame = bytes.fromhex(match[1])
    except ValueError:
        return None
    if (len(frame) != 53 or frame[:3] != bytes((STATION3_SLAVE, 4, 48)) or
            crc16(frame[:-2]) != int.from_bytes(frame[-2:], 'little')):
        return None
    return int.from_bytes(frame[3:5], 'big')


def parse_ntc_temperatures(line):
    match = NTC_TEMPERATURES.search(ANSI.sub('', line))
    if not match:
        return None
    values = tuple(float(value) for value in match.groups())
    if any(not math.isfinite(value) or value < -40 or value > 200
           for value in values):
        return None
    return values


class Recorder:
    def __init__(self, directory, process='工艺1'):
        directory = Path(directory); directory.mkdir(parents=True, exist_ok=True)
        stem = datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        self.csv_path = directory / (stem + '.csv')
        self.raw_path = directory / (stem + '-serial.txt')
        self.csv_file = self.csv_path.open('w', encoding='utf-8-sig', newline='')
        self.raw_file = self.raw_path.open('w', encoding='utf-8')
        self.process = process
        self.raw('工艺：' + process)
        self.writer = csv.writer(self.csv_file)
        calibration = Path(__file__).resolve().parents[3] / 'function/ntcTemp'
        # 控制温度始终来自温度串口；机器型号只用于日志中的本地拟合列。
        # 不读取固件 MACHINE 宏，避免串口连接依赖固件工程配置。
        self.machine = 4
        ntc2_name = {1:'ntcTemp_table.inc',2:'ntcTemp_machine2_table.inc',4:'ntcTemp_machine4_table.inc',6:'ntcTemp_machine6_table.inc'}[self.machine]
        names = ('ntcTemp_ntc1_table.inc', ntc2_name)
        table_directory = calibration / 'tables' / 'NTC2'
        self.tables = [ [(float(a),float(t)) for a,t in re.findall(r'\{([\d.]+)f, ([\d.]+)f\}', (table_directory/name).read_text())] for name in names ]
        self.writer.writerow(('记录时间', '事件来源', '真实温度℃', '真实温度接收时间',
                              'ESP32板级温度℃', '板级温度接收时间',
                              '板级温度误差℃',
                              'NTC1_ADC', 'NTC2_ADC', 'NTC3_ADC', 'NTC接收时间', '模式',
                              'NTC1本地拟合℃','NTC2本地拟合℃','本地MACHINE',
                              'NTC计算温度接收时间', 'NTC1查表温度℃',
                              'NTC2查表温度℃', 'NTC3查表温度℃',
                              'NTC1误差℃', 'NTC2误差℃', 'NTC3误差℃',
                              '站3电机转速RPM', '站3快照接收时间', '工艺'))
        self.csv_file.flush()

    def raw(self, message):
        self.raw_file.write(datetime.now().isoformat(timespec='milliseconds') + ' ' + message + '\n')
        self.raw_file.flush()

    def sample(self, source, temperature, temperature_time, ntcs, ntc_time, mode,
               ntc_temperatures=None, ntc_temperature_time='',
               motor_rpm=None, motor_rpm_time='',
               board_temperature=None, board_temperature_time=''):
        errors = (
            tuple(round(value - temperature, 3) for value in ntc_temperatures)
            if temperature is not None and ntc_temperatures is not None
            else ('', '', '')
        )
        board_error = (
            round(board_temperature - temperature, 3)
            if board_temperature is not None and temperature is not None
            else ''
        )
        self.writer.writerow((datetime.now().isoformat(timespec='milliseconds'), source,
                              '' if temperature is None else temperature, temperature_time,
                              '' if board_temperature is None else board_temperature,
                              board_temperature_time, board_error,
                              *(ntcs or ('', '', '')), ntc_time, mode,
                              lookup(self.tables[0],ntcs[0]) if ntcs else '',
                              lookup(self.tables[1],ntcs[1],True) if ntcs else '',self.machine,
                              ntc_temperature_time if ntc_temperatures else '',
                              *(ntc_temperatures or ('', '', '')), *errors,
                              '' if motor_rpm is None else motor_rpm,
                              motor_rpm_time if motor_rpm is not None else '',
                              self.process))
        self.csv_file.flush()

    def close(self):
        self.csv_file.close(); self.raw_file.close()
