"""Serial temperature parsing and forward-only HeatSoyMilk control."""
import json
import math
import re
from pathlib import Path

from telemetry import crc16

AUTO_HEAT_CONFIG_PATH = Path(__file__).with_name("auto_heat_config.json")
PROCESS_2_CONFIG_PATH = Path(__file__).with_name("auto_heat_process2.json")
PROCESS_3_CONFIG_PATH = Path(__file__).with_name("auto_heat_process3.json")
PROCESS_4_CONFIG_PATH = Path(__file__).with_name("auto_heat_process4.json")
MANUAL_COMMANDS = {
    "开阀": "NB+VALVE=OPEN\r\n",
    "关阀": "NB+VALVE=CLOSE\r\n",
    "蠕动泵正转": "NB+HOSEPUMP=FORWARD\r\n",
    "蠕动泵反转": "NB+HOSEPUMP=REVERSE\r\n",
    "蠕动泵停止": "NB+HOSEPUMP=STOP\r\n",
}
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
TEMP = re.compile(r"(?:\bT\s*=|\bTEMP(?:ERATURE)?\s*[:=]|\bnow\s*:)\s*(-?\d+(?:\.\d+)?)", re.I)
MODBUS_RX = re.compile(r"MODBUS-RX:\[([0-9A-Fa-f\s]+)\]")
STATION3_SLAVE = 0x03
STATION3_HOLD_ENABLE = 0x0000
STATION3_HOLD_CALIBRATION_TRIGGER = 0x003C
STATION3_HOLD_CALIBRATION_STATUS = 0x003D
STATION3_HOLD_HEATER_GEAR = 0x0002
STATION3_HOLD_MOTOR_GEAR = 0x0004


def load_auto_heat_config(path=AUTO_HEAT_CONFIG_PATH):
    try:
        with Path(path).open("r", encoding="utf-8") as stream:
            config = json.load(stream)
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"无法读取自动加热配置 {path}：{error}") from error

    if not isinstance(config, dict) or not isinstance(config.get("stages"), list):
        raise ValueError("自动加热配置必须包含 stages 数组")
    stages = config["stages"]
    required = ("temperature_below_c", "heater_gear", "heater_on_seconds",
                "heater_off_seconds", "motor_gear", "motor_on_seconds",
                "motor_off_seconds")
    previous_limit = 0
    for index, stage in enumerate(stages, 1):
        if not isinstance(stage, dict) or any(key not in stage for key in required):
            raise ValueError(f"自动加热第{index}段配置字段缺失")
        values = [stage[key] for key in required]
        if any(isinstance(value, bool) or not isinstance(value, (int, float))
               or not math.isfinite(value) for value in values):
            raise ValueError(f"自动加热第{index}段参数必须为有限数字")
        limit = stage["temperature_below_c"]
        if (limit <= previous_limit or not 0 <= stage["heater_gear"] <= 30 or
                not 0 <= stage["motor_gear"] <= 15 or
                min(stage["heater_on_seconds"], stage["heater_off_seconds"],
                    stage["motor_on_seconds"], stage["motor_off_seconds"]) < 0 or
                stage["heater_on_seconds"] + stage["heater_off_seconds"] <= 0 or
                stage["motor_on_seconds"] + stage["motor_off_seconds"] <= 0):
            raise ValueError(f"自动加热第{index}段参数超出范围或周期无效")
        previous_limit = limit
    if not stages:
        raise ValueError("自动加热配置至少需要一个温度段")
    return tuple(stages)


STAGES = load_auto_heat_config()
LIMITS = tuple(stage["temperature_below_c"] for stage in STAGES)
PROCESS_STAGES = {"工艺1": STAGES, "工艺2": load_auto_heat_config(PROCESS_2_CONFIG_PATH),
                  "工艺3": load_auto_heat_config(PROCESS_3_CONFIG_PATH),
                  "工艺4": load_auto_heat_config(PROCESS_4_CONFIG_PATH)}


def _modbus_gateway_request(payload):
    frame = payload + crc16(payload).to_bytes(2, "little")
    return f"MODBUS-TX:[{frame.hex(' ').upper()}]\r\n"


def station3_write_register(address, value):
    payload = bytes((
        STATION3_SLAVE, 0x06, address >> 8, address & 0xFF,
        value >> 8, value & 0xFF,
    ))
    return _modbus_gateway_request(payload)


def station3_read_register(address):
    payload = bytes((STATION3_SLAVE, 0x03, address >> 8, address & 0xFF, 0, 1))
    return _modbus_gateway_request(payload)


def is_operation_command(command):
    """Keep writes and NB actions in recent history; exclude read requests."""
    text = command.strip().upper()
    if text.startswith('NB+'):
        return '?' not in text
    match = re.search(r"MODBUS-TX:\[([0-9A-F\s]+)\]", text)
    if not match:
        return False
    try:
        frame = bytes.fromhex(match[1])
    except ValueError:
        return False
    return len(frame) >= 8 and frame[1] in (0x05, 0x06, 0x0F, 0x10, 0x16, 0x17)


def describe_command(command):
    """Explain requested actions; sending does not imply device confirmation."""
    if not is_operation_command(command):
        return ''
    text = command.strip().upper()
    actions = {
        'NB+VALVE=OPEN': '将阀门设置为开启状态',
        'NB+VALVE=CLOSE': '将阀门设置为关闭状态',
        'NB+HOSEPUMP=FORWARD': '开启蠕动泵正转，速度设为90%',
        'NB+HOSEPUMP=REVERSE': '开启蠕动泵反转，速度设为90%',
        'NB+HOSEPUMP=STOP': '将蠕动泵设置为停止状态',
        'NB+CALIBRATE=1': '启动站3加热功率自校准模式',
    }
    if text in actions:
        return actions[text]
    mode = re.fullmatch(r'NB\+SOYMODE=(\d+),([01])', text)
    if mode:
        number, enabled = int(mode[1]), int(mode[2])
        if number == 0:
            return '关闭总开关，停止全部加热和电机，退出固件豆浆运行模式'
        return (f'开启固件豆浆运行模式{number}（启用该模式的加热与电机控制）'
                if enabled else f'请求停止固件豆浆运行模式{number}（仅当前模式匹配时生效）')
    if text.startswith('NB+'):
        return '发送设备操作：' + text[3:]
    match = re.search(r"MODBUS-TX:\[([0-9A-F\s]+)\]", text)
    frame = bytes.fromhex(match[1])
    station, function = frame[:2]
    address = int.from_bytes(frame[2:4], 'big')
    value = int.from_bytes(frame[4:6], 'big')
    if station == 3 and function == 6:
        if address == STATION3_HOLD_ENABLE and value in (0, 1):
            return ('开启站3总开关，允许加热与电机输出（不设置运行档位）'
                    if value else '关闭站3总开关，停止加热与电机并清零运行档位')
        if address in (STATION3_HOLD_HEATER_GEAR, 3):
            heater = '下加热' if address == STATION3_HOLD_HEATER_GEAR else '上加热'
            return f'将站3{heater}设置为{value}档' + ('（关闭加热）' if value == 0 else '')
        if address == STATION3_HOLD_MOTOR_GEAR:
            gear = int.from_bytes(frame[4:6], 'big', signed=True)
            if gear == 0:
                return '将站3电机设置为停止状态（0档）'
            return f'将站3电机设置为{"正转" if gear > 0 else "反转"}{abs(gear)}档'
        if address == STATION3_HOLD_CALIBRATION_TRIGGER:
            return ('启动站3加热功率自校准模式' if value == 1 else
                    '将站3自校准触发值设为0（不触发校准）' if value == 0 else
                    f'将站3自校准触发值设置为{value}')
    if function == 6:
        return f'将站{station}保持寄存器0x{address:04X}设置为{value}'
    if function == 5:
        return f'将站{station}线圈0x{address:04X}设置为' + ('开启' if value == 0xFF00 else '关闭' if value == 0 else f'数值{value}')
    return f'向站{station}下发批量/掩码写入操作，起始地址0x{address:04X}，功能码0x{function:02X}（数值见原始报文）'


def parse_modbus_gateway_response(line):
    match = MODBUS_RX.search(line)
    if not match:
        return None
    try:
        frame = bytes.fromhex(match.group(1))
    except ValueError:
        return None
    if len(frame) < 5 or crc16(frame[:-2]) != int.from_bytes(frame[-2:], "little"):
        return None
    return frame


def parse_temperature(line):
    match = TEMP.search(ANSI.sub("", line))
    if not match:
        return None
    value = float(match[1])
    return value if math.isfinite(value) and 0 <= value <= 150 else None


def parse_heater_gear(line):
    match = re.search(r"\bHEAT_GEAR=(\d+)\b", line, re.I)
    if not match:
        return None
    gear = int(match.group(1))
    return gear if gear <= 30 else None


def mode_for(value):
    return next((i for i, limit in enumerate(LIMITS, 1) if value < limit),
                len(LIMITS) if value <= LIMITS[-1] else 0)


class Controller:
    CALIBRATION_TIMEOUT = 60.0
    CALIBRATION_POLL_INTERVAL = 1.0
    STATION3_REQUEST_TIMEOUT = 3.0
    STATION3_MAX_TIMEOUTS = 3
    COOLDOWN_MOTOR_ON_SECONDS = 3.0
    COOLDOWN_MOTOR_OFF_SECONDS = 1.5

    def __init__(self, send, notify, stages=None, process="工艺1", on_temperature_timeout=None):
        self.send, self.notify = send, notify
        self.on_temperature_timeout = on_temperature_timeout
        self.temperature_timeout_prompted = False
        if process not in PROCESS_STAGES:
            raise ValueError(f"未知自动加热工艺：{process}")
        self.process = process
        self.stages = tuple(PROCESS_STAGES[process] if stages is None else stages)
        if not self.stages:
            raise ValueError("自动加热配置至少需要一个温度段")
        self.limits = tuple(stage["temperature_below_c"] for stage in self.stages)
        self.temperature = None
        self.last_temperature = None
        self.active = False
        self.mode = 0
        self.pending = None
        self.stop_confirmed = False
        self.timeout = 10.0
        self.calibration = None
        self.calibration_started = None
        self.calibration_last_poll = None
        self.calibration_request_at = None
        self.calibration_status_timeouts = 0
        self.write_pending = None
        self.write_timeouts = 0
        self.heater_gear = 0
        self.motor_gear = 0
        self.motor_confirmed_gear = None
        self.desired_heater_gear = 0
        self.desired_motor_gear = 0
        self.mode_started = None
        self.shutdown_queue = None
        self.cooling_down = False
        self.cooldown_phase = None
        self.cooldown_phase_started = None

    def temperature_timeout(self):
        return self.timeout

    def handle_temperature_timeout(self, now):
        if self.on_temperature_timeout is None:
            self.stop(now, self.temperature_timeout_message(now))
        elif not self.temperature_timeout_prompted:
            self.temperature_timeout_prompted = True
            message = self.temperature_timeout_message(now)
            self.notify(message + '；维持当前阶段和开停节奏，等待是否结束的选择')
            self.on_temperature_timeout(self, message)

    def temperature_remaining(self, now):
        if self.last_temperature is None:
            return None
        return max(0.0, self.temperature_timeout() - (now - self.last_temperature))

    def temperature_timeout_message(self, now):
        age = (f"{now - self.last_temperature:.2f}秒"
               if self.last_temperature is not None else "无有效采样")
        return (f"真实温度采集超时：最后有效值={self.temperature}℃，"
                f"已等待{age}，限时={self.temperature_timeout():g}秒")

    def fresh(self, now):
        return (self.last_temperature is not None and
                now - self.last_temperature <= self.temperature_timeout())

    def command(self, mode, now):
        if mode == 0:
            self.stop_confirmed = False
        self.pending = (mode, now)
        self.send(f"NB+SOYMODE={mode},{int(mode != 0)}\r\n")

    def send_station3_write(self, address, value, now):
        self.write_pending = (address, value, now)
        self.write_timeouts = 0
        self.send(station3_write_register(address, value))

    def retry_station3_write(self, now):
        address, value, _ = self.write_pending
        self.write_timeouts += 1
        if self.write_timeouts >= self.STATION3_MAX_TIMEOUTS:
            self.write_pending = None
            self.write_timeouts = 0
            if self.shutdown_queue is not None:
                self.shutdown_queue = None
                self.notify("站3Modbus停止指令连续3次无应答，未确认全停")
            elif self.active or self.cooling_down:
                self.stop(now, "站3Modbus档位指令连续3次无应答，停止自动控制")
            return
        self.write_pending = (address, value, now)
        self.notify(
            f"站3Modbus写入第{self.write_timeouts}次超时，重发保持寄存器"
            f"0x{address:04X}={value}")
        self.send(station3_write_register(address, value))

    def start(self, now):
        if self.active or self.pending:
            raise ValueError("请等待当前指令应答或先断开重连")
        if not self.fresh(now):
            raise ValueError("没有新鲜有效温度，请检查温度串口")
        if self.temperature > 100:
            raise ValueError("当前温度超过100℃，无法启动")
        self.active = True
        self.temperature_timeout_prompted = False
        self.mode = 0
        self.calibration = "stopping"
        self.notify(f"{self.process}启动前全停，等待设备确认")
        self.command(0, now)

    def stop(self, now, reason="手动停止", shutdown_commands=None):
        self.active = False
        self.cooling_down = False
        self.cooldown_phase = None
        self.cooldown_phase_started = None
        self.mode = 0
        self.calibration = None
        self.calibration_request_at = None
        self.write_pending = None
        self.desired_heater_gear = 0
        self.desired_motor_gear = 0
        self.mode_started = None
        self.stop_confirmed = False
        self.shutdown_queue = shutdown_commands or [(STATION3_HOLD_ENABLE, 0)]
        self.notify(reason + "，等待全停确认")
        self.service_shutdown(now)

    def begin_cooldown(self, now):
        self.active = False
        self.cooling_down = True
        self.cooldown_phase = "running" if self.motor_gear == 1 else None
        self.cooldown_phase_started = now
        self.mode = 0
        self.calibration = None
        self.calibration_request_at = None
        self.write_pending = None
        self.write_timeouts = 0
        self.shutdown_queue = None
        self.desired_heater_gear = 0
        self.desired_motor_gear = 0
        self.mode_started = None
        self.notify(f"{self.process}达到100℃，关闭加热并启动电机降温")
        self.service_cooldown(now)

    def finish_cooldown(self, now):
        self.stop(
            now,
            "温度降至89℃或以下，降温完成" if self.process in ("工艺2", "工艺3")
            else "温度降至85℃或以下，冷却完成",
            [(STATION3_HOLD_MOTOR_GEAR, 0), (STATION3_HOLD_ENABLE, 0)],
        )

    def service_cooldown(self, now):
        if (not self.cooling_down or self.write_pending is not None or
                self.shutdown_queue is not None):
            return
        if self.heater_gear != 0:
            self.send_station3_write(STATION3_HOLD_HEATER_GEAR, 0, now)
            return
        motor_on = 3.0 if self.process in ("工艺2", "工艺3") else self.COOLDOWN_MOTOR_ON_SECONDS
        motor_off = (0.5 if self.process == "工艺4" else
                     2.0 if self.process in ("工艺2", "工艺3") else self.COOLDOWN_MOTOR_OFF_SECONDS)
        if self.cooldown_phase is None:
            self.send_station3_write(STATION3_HOLD_MOTOR_GEAR, 1, now)
        elif (self.cooldown_phase == "running" and
              now - self.cooldown_phase_started >= motor_on):
            self.send_station3_write(STATION3_HOLD_MOTOR_GEAR, 0, now)
        elif (self.cooldown_phase == "resting" and
              now - self.cooldown_phase_started >= motor_off):
            self.send_station3_write(STATION3_HOLD_MOTOR_GEAR, 1, now)

    def receive_temperature(self, value, now):
        self.temperature_timeout_prompted = False
        self.temperature = value
        self.last_temperature = now
        if self.cooling_down:
            if value <= (89 if self.process in ("工艺2", "工艺3") else 85):
                self.finish_cooldown(now)
            else:
                self.service_cooldown(now)
        elif self.active and value >= 100:
            self.begin_cooldown(now)
        elif self.active and self.pending is None and self.calibration is None:
            self.advance(now)

    def advance(self, now):
        if not self.fresh(now):
            self.handle_temperature_timeout(now)
            return
        if self.temperature >= 100:
            self.begin_cooldown(now)
            return
        next_mode = next((i for i, limit in enumerate(self.limits, 1)
                          if self.temperature < limit),
                         len(self.limits) if self.temperature <= self.limits[-1] else 0)
        if (self.process == "工艺2" and 3 <= self.mode < len(self.stages) and
                self.mode_started is not None and now - self.mode_started > 720):
            next_mode = max(next_mode, self.mode + 1)
            self.notify(f"工艺2第{self.mode}段超过720秒，进入下一段")
        if next_mode > self.mode:
            self.mode = next_mode
            self.mode_started = now
            self.notify(f"{self.process}进入运行模式{next_mode}，通过站3Modbus控制")
            self.update_automatic_outputs(now)

    def update_automatic_outputs(self, now):
        if not self.active or self.calibration is not None or self.mode == 0:
            return
        elapsed = max(0.0, now - self.mode_started)
        stage = self.stages[self.mode - 1]
        motor_period = stage["motor_on_seconds"] + stage["motor_off_seconds"]
        heater_period = stage["heater_on_seconds"] + stage["heater_off_seconds"]
        self.desired_motor_gear = (
            stage["motor_gear"]
            if elapsed % motor_period < stage["motor_on_seconds"] else 0)
        self.desired_heater_gear = (
            stage["heater_gear"]
            if elapsed % heater_period < stage["heater_on_seconds"] else 0)
        self.service_automatic_outputs(now)

    def service_automatic_outputs(self, now):
        if (self.write_pending is not None or self.calibration is not None or
                self.shutdown_queue is not None):
            return
        if self.heater_gear != self.desired_heater_gear:
            self.send_station3_write(STATION3_HOLD_HEATER_GEAR,
                                     self.desired_heater_gear, now)
        elif self.motor_gear != self.desired_motor_gear:
            self.send_station3_write(STATION3_HOLD_MOTOR_GEAR,
                                     self.desired_motor_gear, now)

    def service_shutdown(self, now):
        if self.write_pending is None and self.shutdown_queue:
            address, value = self.shutdown_queue[0]
            self.send_station3_write(address, value, now)

    def receive_ack(self, line, now):
        if line.strip().upper().startswith("ERROR:"):
            if self.write_pending is not None:
                self.notify("站3Modbus未收到有效回显，将在请求超时后重发")
            elif self.calibration == "running" and self.calibration_request_at is not None:
                self.notify("站3校准状态读取失败，将在请求超时后重试")
            return

        match = re.search(r"NB\+SOYMODE=(\d+),([01]) OK", line)
        if not match or self.pending is None:
            return
        mode, on = map(int, match.groups())
        if mode != self.pending[0] or on != int(mode != 0):
            return
        self.pending = None
        self.mode = mode
        if mode == 0 and self.shutdown_queue is None:
            self.stop_confirmed = True
            self.heater_gear = 0
            self.motor_gear = 0
        self.notify("设备已确认全停" if mode == 0 else
                    f"运行模式{mode}，目标{self.limits[mode-1]}℃")
        if self.active:
            if mode == 0 and self.calibration == "stopping":
                self.calibration = "enabling"
                self.calibration_started = now
                self.calibration_last_poll = now
                self.calibration_request_at = None
                self.notify("通过站3Modbus打开总开关")
                self.send_station3_write(STATION3_HOLD_ENABLE, 1, now)
            elif self.calibration is not None:
                return
            elif self.temperature > 100:
                self.begin_cooldown(now)
            else:
                self.advance(now)

    def receive_modbus(self, line, now):
        frame = parse_modbus_gateway_response(line)
        if frame is None or frame[0] != STATION3_SLAVE:
            return
        if frame[1] & 0x80:
            if self.shutdown_queue is not None:
                self.write_pending = None
                self.shutdown_queue = None
                self.notify("站3Modbus停止指令异常，未确认全停")
            elif self.calibration == "running" and self.write_pending is None:
                self.calibration_request_at = None
                self.calibration_last_poll = now
                self.notify("站3校准状态查询异常，等待下次重试")
            elif self.calibration in ("enabling", "triggering", "running") or self.write_pending:
                self.write_pending = None
                self.stop(now, f"站3Modbus异常响应 0x{frame[2]:02X}，停止自动控制")
            return

        if frame[1] == 0x06 and self.write_pending is not None:
            address, value, _ = self.write_pending
            if frame[2:6] == bytes((address >> 8, address & 0xFF,
                                    value >> 8, value & 0xFF)):
                self.write_pending = None
                self.write_timeouts = 0
                if address == STATION3_HOLD_ENABLE and value == 0:
                    self.motor_confirmed_gear = 0
                    self.notify("站3总开关关闭已确认，电机档位清零")
                if address == STATION3_HOLD_MOTOR_GEAR:
                    self.motor_confirmed_gear = int.from_bytes(frame[4:6], 'big', signed=True)
                    self.notify(f"站3电机档位已确认：{self.motor_confirmed_gear}（0=停止，正数正转，负数反转）")
                if self.shutdown_queue is not None:
                    if self.shutdown_queue[0] == (address, value):
                        self.shutdown_queue.pop(0)
                        if not self.shutdown_queue:
                            self.shutdown_queue = None
                            self.stop_confirmed = True
                            self.heater_gear = self.motor_gear = 0
                            self.notify("站3Modbus已确认全停")
                    self.service_shutdown(now)
                elif address == STATION3_HOLD_HEATER_GEAR:
                    self.heater_gear = value
                    self.notify(f"站3下加热档位已确认：{value}")
                elif address == STATION3_HOLD_MOTOR_GEAR:
                    self.motor_gear = value
                    if self.cooling_down:
                        self.cooldown_phase = "running" if value == 1 else "resting"
                        self.cooldown_phase_started = now
                if self.shutdown_queue is None:
                    if self.cooling_down:
                        self.service_cooldown(now)
                    else:
                        self.service_automatic_outputs(now)

        if self.calibration not in ("enabling", "triggering", "running"):
            return
        if self.calibration == "enabling":
            if frame[1] != 0x06 or frame[2:6] != bytes((0, 0, 0, 1)):
                return
            self.calibration = "triggering"
            self.calibration_request_at = None
            self.notify("站3总开关已打开，通过Modbus启动校准")
            self.send_station3_write(STATION3_HOLD_CALIBRATION_TRIGGER, 1, now)
            return

        if self.calibration == "triggering":
            if frame[1] != 0x06 or frame[2:6] != bytes((0, 0x3C, 0, 1)):
                return
            self.calibration = "running"
            self.calibration_started = now
            self.calibration_last_poll = now
            self.calibration_request_at = None
            self.calibration_status_timeouts = 0
            self.write_pending = None
            self.notify("站3校准已触发，等待校准完成")
            return

        if frame[1] != 0x03 or len(frame) != 7 or frame[2] != 2:
            return
        if self.calibration_request_at is None:
            return
        status = int.from_bytes(frame[3:5], "big")
        self.calibration_request_at = None
        self.calibration_status_timeouts = 0
        if status == 2:
            self.calibration = None
            self.notify("站3校准完成，开始自动加热")
            if self.active:
                self.advance(now)
        elif status == 3:
            self.stop(now, "站3校准超时，未启动自动加热")
        elif status == 1:
            self.calibration_started = now
            self.notify("站3正在校准")

    def tick(self, now):
        if self.active and self.calibration in ("enabling", "triggering", "running"):
            if now - self.calibration_started >= self.CALIBRATION_TIMEOUT:
                self.stop(now, "站3校准状态60秒未确认校准中，未启动自动加热")
            elif (self.calibration == "running" and
                  self.calibration_request_at is not None and
                  now - self.calibration_request_at > self.STATION3_REQUEST_TIMEOUT):
                self.calibration_status_timeouts += 1
                self.calibration_request_at = now
                self.calibration_last_poll = now
                self.notify(
                    f"站3校准状态读取第{self.calibration_status_timeouts}次超时，重试")
                self.send(station3_read_register(STATION3_HOLD_CALIBRATION_STATUS))
            elif (self.calibration == "running" and
                  self.calibration_request_at is None and
                  now - self.calibration_last_poll >= self.CALIBRATION_POLL_INTERVAL):
                self.calibration_request_at = now
                self.calibration_last_poll = now
                self.send(station3_read_register(STATION3_HOLD_CALIBRATION_STATUS))
        elif self.active and self.calibration is None:
            if self.process == "工艺2":
                self.advance(now)
            self.update_automatic_outputs(now)
        elif self.cooling_down:
            self.service_cooldown(now)
        if self.shutdown_queue is not None:
            self.service_shutdown(now)
        if (self.write_pending is not None and
                now - self.write_pending[2] > self.STATION3_REQUEST_TIMEOUT):
            self.retry_station3_write(now)
        if self.pending and now - self.pending[1] > 3:
            mode = self.pending[0]
            self.pending = None
            if mode:
                self.stop(now, "模式指令未应答")
            else:
                self.active = False
                self.notify("全停未确认，请检查设备并手动确认已停机")
        elif (self.active and not self.fresh(now) and
              (self.on_temperature_timeout is not None or self.write_pending is None) and
              not (self.calibration == "running" and
                   self.calibration_request_at is not None)):
            self.handle_temperature_timeout(now)
