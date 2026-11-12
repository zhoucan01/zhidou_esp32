import unittest
from control import Controller, MANUAL_COMMANDS, describe_command, station3_write_register
from telemetry import request_station3_snapshot


class TestMotorStatus(unittest.TestCase):
    def test_write_confirmation_and_stop(self):
        sent, messages = [], []
        controller = Controller(sent.append, messages.append)
        self.assertIsNone(controller.motor_confirmed_gear)
        controller.send_station3_write(4, 1, 0)
        self.assertIsNone(controller.motor_confirmed_gear)
        controller.receive_modbus(sent[-1].replace('MODBUS-TX:', 'MODBUS-RX:'), 0.2)
        self.assertEqual(controller.motor_confirmed_gear, 1)
        self.assertTrue(any('电机档位已确认：1' in line for line in messages))
        controller.send_station3_write(0, 0, 1)
        controller.receive_modbus(sent[-1].replace('MODBUS-TX:', 'MODBUS-RX:'), 1.2)
        self.assertEqual(controller.motor_confirmed_gear, 0)

    def test_descriptions(self):
        for value, word in ((0, '停止'), (1, '正转'), (65535, '反转')):
            self.assertIn(word, describe_command(station3_write_register(4, value)))
        self.assertEqual(describe_command(request_station3_snapshot()), '')

    def test_all_host_operations_have_chinese_meaning(self):
        cases = [(0, 0, '关闭站3总开关'), (0, 1, '开启站3总开关'),
                 (2, 7, '下加热设置为7档'), (2, 0, '关闭加热'),
                 (0x3c, 1, '自校准模式')]
        for address, value, expected in cases:
            self.assertIn(expected, describe_command(station3_write_register(address, value)))
        for label, command in MANUAL_COMMANDS.items():
            description = describe_command(command)
            self.assertNotIn('NB+', description)
            self.assertTrue('阀门' in description or '蠕动泵' in description)
        self.assertIn('停止全部加热和电机', describe_command('NB+SOYMODE=0,0'))
        self.assertIn('开启固件豆浆运行模式3', describe_command('NB+SOYMODE=3,1'))
        self.assertIn('请求停止固件豆浆运行模式3', describe_command('NB+SOYMODE=3,0'))
        self.assertEqual(describe_command('NB+status=?'), '')

    def test_interleaved_log_is_not_a_valid_ack(self):
        controller = Controller(lambda command: None, lambda message: None)
        controller.send_station3_write(4, 1, 0)
        controller.receive_modbus('MODBUS-RX:[\x1b[0;33mW (492159) DataCenter: Recover station 3', 0.2)
        controller.receive_modbus('03 06 00 04 00 01 08 29]', 0.3)
        self.assertIsNotNone(controller.write_pending)
        self.assertIsNone(controller.motor_confirmed_gear)
