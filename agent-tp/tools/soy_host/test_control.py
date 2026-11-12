import unittest
import json
import tempfile
from pathlib import Path
from control import (
    Controller, MANUAL_COMMANDS, PROCESS_STAGES, STAGES, load_auto_heat_config, mode_for,
    parse_heater_gear, parse_temperature, station3_read_register,
    station3_write_register,
)
from telemetry import crc16


def modbus_response(payload):
    frame = payload + crc16(payload).to_bytes(2, 'little')
    return 'MODBUS-RX:[' + ' '.join(f'{byte:02X}' for byte in frame) + ']'


def echo_request(command):
    frame = bytes.fromhex(command.split('[', 1)[1].split(']', 1)[0])
    return modbus_response(frame[:-2])


def status_response(status):
    return modbus_response(bytes((0x03, 0x03, 0x02, 0, status)))


class TestControl(unittest.TestCase):
    def test_process4_heating_and_motor_timing(self):
        c = Controller(self.commands.append, self.messages.append, process='工艺4')
        c.active = True
        for temperature, mode, gear in ((59, 1, 14), (60, 2, 14), (94.9, 2, 14),
                                       (95, 3, 8), (98, 3, 8), (99.9, 3, 8)):
            c.receive_temperature(temperature, temperature)
            c.update_automatic_outputs(temperature)
            self.assertEqual(c.mode, mode)
            self.assertEqual(c.desired_heater_gear, gear)
        start = c.mode_started
        for elapsed, motor in ((2.9, 1), (3, 0), (3.49, 0), (3.5, 1)):
            c.update_automatic_outputs(start + elapsed)
            self.assertEqual(c.desired_motor_gear, motor)
            self.assertEqual(c.desired_heater_gear, 8)
        c.receive_temperature(100, 101)
        self.assertTrue(c.cooling_down)
        self.assertFalse(c.active)
        self.assertTrue(all(command.startswith('MODBUS-TX:') for command in self.commands))

    def test_all_processes_keep_final_stage_until_100(self):
        for process, stages in PROCESS_STAGES.items():
            with self.subTest(process=process):
                self.assertEqual(stages[-1]['temperature_below_c'], 100)
                c = Controller(lambda command: None, lambda message: None, process=process)
                c.active = True
                c.receive_temperature(96, 0)
                for temperature, now in ((98, 10), (99.9, 800)):
                    c.receive_temperature(temperature, now)
                    self.assertTrue(c.active)
                    self.assertFalse(c.cooling_down)
                    self.assertEqual(c.mode, len(stages))
                c.receive_temperature(100, 801)
                self.assertFalse(c.active)
                self.assertTrue(c.cooling_down)

    def setUp(self):
        self.commands=[];self.messages=[]
        self.c=Controller(self.commands.append,self.messages.append)

    def test_parser(self):
        self.assertEqual(parse_temperature('[21:00:00]收←◆T=86.7'),86.7)
        self.assertEqual(parse_temperature('\x1b[32mI 60 temp now:62.3,num:1'),62.3)
        for s in ('T=-600.0','T=151','HEARTBEAT:1234','NB+SOYMODE=1,1 OK'):
            self.assertIsNone(parse_temperature(s))

    def test_boundaries(self):
        self.assertEqual([mode_for(t) for t in (59.9,60,80,85,90,93,95,98,100,100.1)],
                         [1,2,3,4,5,6,7,7,7,0])

    def test_auto_heat_config_contains_each_stage_timing(self):
        self.assertEqual([stage["temperature_below_c"] for stage in STAGES],
                         [60, 80, 85, 90, 93, 95, 100])
        self.assertEqual(
            [(stage["heater_gear"], stage["heater_on_seconds"],
              stage["heater_off_seconds"], stage["motor_on_seconds"],
              stage["motor_off_seconds"]) for stage in STAGES],
            [(19, 1, 0, 2, 6), (10, 1, 0, 2, 6), (10, 6, 4, 2, 6),
             (9, 6, 15, 2, 6), (9, 6, 4, 2, 6), (8, 6, 4, 2, 6),
             (8, 6, 4, 2, 6)])

    def test_process3_table_and_temperature_boundaries(self):
        stages = PROCESS_STAGES['工艺3']
        self.assertEqual(
            [(s['temperature_below_c'], s['heater_gear'],
              s['heater_on_seconds'], s['heater_off_seconds']) for s in stages],
            [(60, 19, 1, 0), (80, 10, 1, 0), (85, 10, 6, 4),
             (90, 9, 6, 15), (93, 9, 6, 4), (95, 7, 5, 5), (100, 7, 6, 4)])
        c = Controller(self.commands.append, self.messages.append, process='工艺3')
        c.active = True
        for temperature, mode in ((59, 1), (60, 2), (80, 3), (85, 4),
                                  (90, 5), (93, 6), (95, 7), (97.9, 7)):
            c.receive_temperature(temperature, temperature)
            self.assertEqual(c.mode, mode)
        c.receive_temperature(97.9, 900)
        self.assertEqual(c.mode, 7)
        self.assertFalse(c.cooling_down)
        c.heater_gear = 7
        c.write_pending = None
        c.receive_temperature(100, 901)
        self.assertTrue(c.cooling_down)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 0))
        c.receive_temperature(89.1, 902)
        self.assertTrue(c.cooling_down)
        c.receive_temperature(89, 903)
        self.assertFalse(c.cooling_down)

    def test_process2_heating_and_motor_timing(self):
        stages = PROCESS_STAGES['工艺2']
        self.assertEqual(
            [(s['temperature_below_c'], s['heater_gear'],
              s['heater_on_seconds'], s['heater_off_seconds'],
              s['motor_on_seconds'], s['motor_off_seconds']) for s in stages],
            [(60, 19, 1, 0, 3, 2), (80, 10, 1, 0, 3, 2),
             (85, 10, 6, 4, 3, 3), (90, 9, 6, 15, 4, 3),
             (93, 9, 6, 4, 3, 3), (95, 7, 5, 5, 3, 3),
             (100, 7, 6, 4, 3, 3)])
        c = Controller(self.commands.append, self.messages.append, process='工艺2')
        c.active = True
        c.receive_temperature(90, 0)
        self.assertEqual(c.mode, 5)
        c.receive_temperature(94, 1)
        self.assertEqual(c.mode, 6)
        c.update_automatic_outputs(6)
        self.assertEqual((c.desired_heater_gear, c.desired_motor_gear), (0, 0))
        c.receive_temperature(100, 7)
        self.assertTrue(c.cooling_down)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 1))
        c.receive_temperature(89, 8)
        self.assertFalse(c.cooling_down)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 0))

    def test_process2_stage_timeout_advances(self):
        c = Controller(self.commands.append, self.messages.append, process='工艺2')
        c.active = True
        c.receive_temperature(82, 0)
        self.assertEqual(c.mode, 3)
        c.receive_temperature(82, 721)
        self.assertEqual(c.mode, 4)
        self.assertFalse(c.cooling_down)

    def test_process2_cooling_turns_heater_off_before_motor_cycle(self):
        c = Controller(self.commands.append, self.messages.append, process='工艺2')
        c.active = True
        c.mode = 7
        c.mode_started = 0
        c.heater_gear = 7
        c.motor_gear = 1
        c.receive_temperature(100, 1)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 0))
        c.receive_modbus(echo_request(self.commands[-1]), 1.1)
        c.tick(4.2)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 0))
        c.receive_modbus(echo_request(self.commands[-1]), 4.3)
        c.tick(4.9)
        self.assertEqual(len(self.commands), 2)
        c.tick(6.2)
        self.assertEqual(len(self.commands), 2)
        c.tick(6.3)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 1))

    def test_auto_heat_config_rejects_invalid_stage(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "auto_heat_config.json"
            path.write_text(json.dumps({"stages": [{"temperature_below_c": 60}]}),
                            encoding="utf-8")
            with self.assertRaises(ValueError):
                load_auto_heat_config(path)

    def test_stage_parameters_control_heater_and_motor_cycles(self):
        stage = dict(STAGES[2])
        stage.update(heater_gear=12, heater_on_seconds=3, heater_off_seconds=2,
                     motor_gear=4, motor_on_seconds=1, motor_off_seconds=2)
        controller = Controller(self.commands.append, self.messages.append, [stage])
        controller.active = True
        controller.mode = 1
        controller.mode_started = 0
        controller.update_automatic_outputs(2)
        self.assertEqual((controller.desired_heater_gear,
                          controller.desired_motor_gear), (12, 0))
        controller.update_automatic_outputs(3)
        self.assertEqual((controller.desired_heater_gear,
                          controller.desired_motor_gear), (0, 4))

    def test_heater_gear_parser(self):
        self.assertEqual(parse_heater_gear(
            'NB+SOYMODE=3 motorNum=1 heatNum=2 HEAT_GEAR=10'), 10)
        self.assertEqual(parse_heater_gear(
            'NB+SOYMODE=3,1 OK HEAT_GEAR=0'), 0)
        self.assertIsNone(parse_heater_gear('HEAT_GEAR=31'))
        self.assertIsNone(parse_heater_gear('SOYMODE=3'))

    def test_manual_nb_commands(self):
        self.assertEqual(MANUAL_COMMANDS, {
            "开阀": "NB+VALVE=OPEN\r\n",
            "关阀": "NB+VALVE=CLOSE\r\n",
            "蠕动泵正转": "NB+HOSEPUMP=FORWARD\r\n",
            "蠕动泵反转": "NB+HOSEPUMP=REVERSE\r\n",
            "蠕动泵停止": "NB+HOSEPUMP=STOP\r\n",
        })

    def test_station3_modbus_requests(self):
        self.assertEqual(station3_write_register(0, 1),
                         'MODBUS-TX:[03 06 00 00 00 01 49 E8]\r\n')
        self.assertEqual(station3_write_register(0x003C, 1),
                         'MODBUS-TX:[03 06 00 3C 00 01 89 E4]\r\n')
        self.assertEqual(station3_read_register(0x003D),
                         'MODBUS-TX:[03 03 00 3D 00 01 14 24]\r\n')

    def start_calibration(self, temperature=65, now=0):
        self.c.receive_temperature(temperature, now)
        self.c.start(now)
        self.c.receive_ack('NB+SOYMODE=0,0 OK HEAT_GEAR=0', now + .1)
        self.assertEqual(self.commands[-1],
                         'MODBUS-TX:[03 06 00 00 00 01 49 E8]\r\n')
        self.c.receive_modbus(echo_request(self.commands[-1]), now + .2)
        self.assertEqual(self.commands[-1],
                         'MODBUS-TX:[03 06 00 3C 00 01 89 E4]\r\n')
        self.c.receive_modbus(echo_request(self.commands[-1]), now + .3)

    def test_start_requires_fresh_temperature_and_stop_ack(self):
        with self.assertRaises(ValueError):self.c.start(0)
        self.start_calibration()
        self.c.tick(1.3)
        self.assertEqual(self.commands[-1],
                         'MODBUS-TX:[03 03 00 3D 00 01 14 24]\r\n')
        self.c.receive_modbus(status_response(1), 1.4)
        self.assertTrue(self.c.active)
        self.c.tick(2.5)
        self.assertEqual(self.commands[-1],
                         'MODBUS-TX:[03 03 00 3D 00 01 14 24]\r\n')
        self.c.receive_modbus(status_response(2), 2.6)
        self.assertEqual(self.commands[-1],
                         station3_write_register(0x0002, 10))
        self.c.receive_modbus(echo_request(self.commands[-1]), 2.7)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 1))
        self.c.receive_modbus(echo_request(self.commands[-1]), 2.8)
        self.c.receive_temperature(64,3)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 1))
        self.c.receive_temperature(90,4)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 9))
        self.c.receive_modbus(echo_request(self.commands[-1]), 4.1)
        self.c.receive_temperature(80,5)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 9))
        self.c.receive_temperature(98,6)
        self.assertTrue(self.c.active)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 8))
        self.c.receive_modbus(echo_request(self.commands[-1]), 6.1)
        self.c.receive_temperature(100,6.2)
        self.assertFalse(self.c.active)
        self.c.receive_temperature(101,6.3)
        self.assertFalse(self.c.active)
        self.assertTrue(self.c.cooling_down)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 0))
        self.c.receive_modbus(echo_request(self.commands[-1]), 6.4)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 0))
        self.c.tick(9.4)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 0))

    def test_overtemperature_cooling_cycle_until_85(self):
        self.start_calibration(temperature=98)
        self.c.tick(1.3)
        self.c.receive_modbus(status_response(2), 1.4)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 8))
        self.c.receive_modbus(echo_request(self.commands[-1]), 1.5)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 1))
        self.c.receive_modbus(echo_request(self.commands[-1]), 1.6)

        self.c.receive_temperature(101, 2.0)
        self.assertTrue(self.c.cooling_down)
        self.assertFalse(self.c.active)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 0))
        self.c.receive_modbus(echo_request(self.commands[-1]), 2.1)
        self.c.tick(4.9)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 0))
        self.c.tick(5.1)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 0))
        self.c.receive_modbus(echo_request(self.commands[-1]), 5.2)
        self.c.tick(6.6)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 0))
        self.c.tick(6.8)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 1))
        self.c.receive_modbus(echo_request(self.commands[-1]), 6.9)

        self.c.receive_temperature(85, 7.0)
        self.assertFalse(self.c.cooling_down)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 0))
        self.c.receive_modbus(echo_request(self.commands[-1]), 7.1)
        self.assertEqual(self.commands[-1], station3_write_register(0x0000, 0))
        self.c.receive_modbus(echo_request(self.commands[-1]), 7.2)
        self.assertTrue(self.c.stop_confirmed)

    def test_automatic_heating_uses_modbus_cycles(self):
        self.start_calibration(temperature=80)
        self.c.tick(1.3)
        self.c.receive_modbus(status_response(2), 1.4)
        self.assertEqual(self.c.mode, 3)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 10))
        self.c.receive_modbus(echo_request(self.commands[-1]), 1.5)
        self.assertEqual(self.commands[-1], station3_write_register(0x0004, 1))
        self.c.receive_modbus(echo_request(self.commands[-1]), 1.6)
        self.c.receive_temperature(80, 7.4)
        self.c.tick(7.5)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 0))
        self.c.receive_modbus(echo_request(self.commands[-1]), 7.6)
        self.assertEqual(self.c.heater_gear, 0)

    def test_station3_requests_retry_until_three_timeouts(self):
        self.c.timeout = 2
        self.start_calibration(temperature=80)
        self.c.tick(1.3)
        self.c.receive_modbus(status_response(2), 1.4)
        gear_command = station3_write_register(0x0002, 10)
        self.assertEqual(self.commands[-1], gear_command)

        self.c.receive_ack("ERROR:RESPONSE_CRC", 1.5)
        self.assertTrue(self.c.active)
        self.c.tick(4.5)
        self.assertEqual(self.commands[-1], gear_command)
        self.assertTrue(self.c.active)
        self.c.tick(7.6)
        self.assertEqual(self.commands[-1], gear_command)
        self.assertTrue(self.c.active)
        self.c.tick(10.7)
        self.assertFalse(self.c.active)
        self.assertEqual(
            self.commands.count(gear_command), 3,
            "initial request plus two retries should precede the third timeout",
        )
        self.assertEqual(self.commands[-1], station3_write_register(0x0000, 0))

    def test_calibration_status_read_retries_until_one_minute(self):
        self.c.timeout = 2
        self.start_calibration()
        status_command = station3_read_register(0x003D)
        self.c.tick(1.3)
        self.assertEqual(self.commands[-1], status_command)
        self.c.tick(4.4)
        self.assertEqual(self.commands[-1], status_command)
        self.assertTrue(self.c.active)
        self.c.tick(7.5)
        self.assertEqual(self.commands[-1], status_command)
        self.assertTrue(self.c.active)
        self.c.tick(10.6)
        self.assertTrue(self.c.active)
        self.assertEqual(self.commands.count(status_command), 4)
        self.c.tick(60.4)
        self.assertFalse(self.c.active)
        self.assertEqual(self.commands[-1], station3_write_register(0x0000, 0))

    def test_fresh_running_status_renews_one_minute_deadline(self):
        self.start_calibration()
        self.c.tick(1.3)
        self.c.receive_modbus(status_response(1), 1.4)
        self.c.tick(51)
        self.assertIsNotNone(self.c.calibration_request_at)
        self.c.receive_modbus(status_response(1), 51.1)
        self.c.tick(70)
        self.assertTrue(self.c.active)
        self.c.tick(111.2)
        self.assertFalse(self.c.active)
        self.assertEqual(self.commands[-1], station3_write_register(0x0000, 0))

    def test_idle_or_unsolicited_status_does_not_renew_deadline(self):
        self.start_calibration()
        self.c.receive_modbus(status_response(1), .5)
        self.assertIsNone(self.c.calibration_request_at)
        self.c.tick(1.3)
        self.c.receive_modbus(status_response(1), 1.4)
        self.c.tick(51)
        self.c.receive_modbus(status_response(0), 51.1)
        self.c.tick(61.5)
        self.assertFalse(self.c.active)

    def test_calibration_timeout_or_failure_never_starts_heating(self):
        self.start_calibration()
        self.c.tick(1.3)
        self.c.receive_modbus(status_response(3), 1.4)
        self.assertEqual(self.commands[-1], station3_write_register(0x0000, 0))
        self.assertFalse(self.c.active)
        self.assertFalse(any('SOYMODE=2,1' in command for command in self.commands))

        self.c=Controller(self.commands.append,self.messages.append)
        self.start_calibration(now=10)
        self.c.tick(110.4)
        self.assertFalse(self.c.active)
        self.assertEqual(self.commands[-1], station3_write_register(0x0000, 0))
        self.assertFalse(any('SOYMODE=2,1' in command for command in self.commands))

    def test_modbus_idle_and_read_exception_do_not_complete(self):
        self.start_calibration()
        self.c.tick(1.3)
        self.c.receive_modbus(status_response(0), 1.4)
        self.assertTrue(self.c.active)
        self.c.tick(2.5)
        self.c.receive_modbus(modbus_response(bytes((3, 0x83, 2))), 2.6)
        self.assertTrue(self.c.active)
        self.c.tick(61)
        self.assertFalse(self.c.active)
        self.assertEqual(self.commands[-1], station3_write_register(0x0000, 0))

    def test_timeout_and_unconfirmed_stop(self):
        self.c.receive_temperature(50,0);self.c.start(0)
        self.c.receive_ack('NB+SOYMODE=0,0 OK',.1)
        self.c.receive_modbus(echo_request(self.commands[-1]),.2)
        self.c.receive_modbus(echo_request(self.commands[-1]),.3)
        self.c.tick(1.3)
        self.c.receive_modbus(status_response(2),1.4)
        self.c.receive_temperature(65,1.5)
        self.c.tick(4.5)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 19))
        self.assertTrue(self.c.active)
        self.c.tick(7.6)
        self.assertEqual(self.commands[-1], station3_write_register(0x0002, 19))
        self.assertTrue(self.c.active)
        self.c.tick(10.7)
        self.assertEqual(self.commands[-1], station3_write_register(0x0000, 0))
        self.assertFalse(self.c.active)
        self.c.tick(13.8)
        self.assertFalse(self.c.stop_confirmed)

    def test_temperature_timeout(self):
        self.c.receive_temperature(50,0);self.c.start(0)
        self.c.receive_ack('NB+SOYMODE=0,0 OK',.1)
        self.c.receive_modbus(echo_request(self.commands[-1]),.2)
        self.c.receive_modbus(echo_request(self.commands[-1]),.3)
        self.c.tick(1.3)
        self.c.receive_modbus(status_response(2),1.4)
        self.c.receive_modbus(echo_request(self.commands[-1]),1.5)
        self.c.receive_modbus(echo_request(self.commands[-1]),1.6)
        self.c.tick(3.5)
        self.c.receive_modbus(echo_request(self.commands[-1]),3.6)
        self.c.tick(6)
        self.assertTrue(self.c.active)
        # Acknowledge any timed output, then isolate freshness from new writes.
        while self.c.write_pending is not None:
            self.c.receive_modbus(echo_request(self.commands[-1]), 6.1)
        self.c.mode = 0
        self.c.tick(11)
        self.assertFalse(self.c.active)
        self.assertEqual(self.commands[-1], station3_write_register(0x0000, 0))


if __name__=='__main__':unittest.main()
