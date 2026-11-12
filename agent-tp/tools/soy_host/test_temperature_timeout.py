"""Temperature freshness boundaries and invalid serial readings."""
import unittest
from control import Controller, parse_temperature, station3_write_register


class TestTemperatureTimeout(unittest.TestCase):
    def test_prompt_continues_without_repeated_prompts_and_rearms_on_recovery(self):
        from control import PROCESS_STAGES
        for process in PROCESS_STAGES:
            sent, prompts = [], []
            c = Controller(sent.append, lambda message: None, process=process,
                           on_temperature_timeout=lambda *args: prompts.append(args))
            c.active = True
            c.receive_temperature(55, 0)
            mode, started = c.mode, c.mode_started
            c.write_pending = None
            c.tick(11)
            self.assertTrue(c.active)
            self.assertEqual(len(prompts), 1)
            c.write_pending = None
            c.tick(12)
            self.assertEqual(len(prompts), 1)
            self.assertEqual((c.mode, c.mode_started), (mode, started))
            self.assertIsNone(c.shutdown_queue)
            c.receive_temperature(56, 13)
            c.write_pending = None
            c.tick(24)
            self.assertEqual(len(prompts), 2)
            c.stop(24, '用户选择结束')
            self.assertFalse(c.active)
            self.assertIsNotNone(c.shutdown_queue)

    def controller(self, temperature, process='工艺1'):
        sent, messages = [], []
        controller = Controller(sent.append, messages.append, process=process)
        controller.receive_temperature(temperature, 100)
        return controller, sent, messages

    def test_threshold_and_countdown(self):
        for temperature, limit in ((25, 10), (79.9, 10), (80, 10), (86.4, 10)):
            with self.subTest(temperature=temperature):
                controller, _, _ = self.controller(temperature)
                self.assertEqual(controller.temperature_timeout(), limit)
                self.assertEqual(controller.temperature_remaining(102), limit - 2)
                self.assertTrue(controller.fresh(100 + limit))
                self.assertFalse(controller.fresh(100 + limit + .01))
                self.assertEqual(controller.temperature_remaining(200), 0)

    def test_no_sample(self):
        controller = Controller(lambda command: None, lambda message: None)
        self.assertIsNone(controller.temperature_remaining(0))
        self.assertFalse(controller.fresh(0))

    def test_invalid_readings_do_not_reset_deadline(self):
        controller, _, _ = self.controller(86.4)
        for now in range(101, 111):
            value = parse_temperature('T=-600.0')
            if value is not None:
                controller.receive_temperature(value, now)
        self.assertEqual(controller.last_temperature, 100)
        self.assertFalse(controller.fresh(110.01))
        controller.receive_temperature(86.5, 111)
        self.assertEqual(controller.temperature_remaining(111), 10)
        controller.receive_temperature(79.9, 112)
        self.assertEqual(controller.temperature_remaining(112), 10)

    def test_both_processes_stop_after_high_temperature_deadline(self):
        for process in ('工艺1', '工艺2'):
            with self.subTest(process=process):
                controller, sent, messages = self.controller(86.4, process)
                controller.active = True
                controller.tick(105.1)
                self.assertTrue(controller.active)
                # Isolate the temperature deadline from outstanding Modbus writes.
                controller.write_pending = None
                controller.tick(110.1)
                self.assertFalse(controller.active)
                self.assertEqual(sent[-1], station3_write_register(0, 0))
                self.assertTrue(any('限时=10秒' in message for message in messages))

    def test_low_temperature_stops_after_ten_seconds(self):
        controller, sent, messages = self.controller(79.9)
        controller.active = True
        controller.tick(105.1)
        self.assertTrue(controller.active)
        controller.tick(110.1)
        self.assertFalse(controller.active)
        self.assertEqual(sent[-1], station3_write_register(0, 0))
        self.assertTrue(any('限时=10秒' in message for message in messages))
