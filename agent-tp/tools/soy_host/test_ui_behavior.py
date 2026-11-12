"""Exercise actual lifecycle/plot methods without requiring a display server."""
import ast
from collections import deque
from datetime import datetime
from control import is_operation_command, station3_write_register
from telemetry import request_ntcs, request_station3_snapshot, request_station1_board_temperature, TemperatureErrorStats
import math
from pathlib import Path
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock

source = ast.parse(Path(__file__).with_name('app.py').read_text())
app_class = next(node for node in source.body if isinstance(node, ast.ClassDef) and node.name == 'App')
methods = {'release', 'disconnect', 'finish_disconnect', 'sample_plot', 'draw', 'scrollable_text', 'record_command'}
app_class.body = [node for node in app_class.body if isinstance(node, ast.FunctionDef) and node.name in methods]
namespace = {'datetime': datetime, 'is_operation_command': is_operation_command, 'time': time, 'math': math, 'BOARD_TEMPERATURE_STALE_SECONDS': 3}
exec(compile(ast.Module(body=[app_class], type_ignores=[]), 'app.py', 'exec'), namespace)
App = namespace['App']


class TestUIBehavior(unittest.TestCase):
    def make_app(self):
        app = App()
        for name in ('quit_reader', 'real_value_var', 'board_value_var', 'error_value_var',
                     'connection_var', 'countdown_value_var', 'heater_value_var', 'motor_value_var',
                     'sensor_table', 'disconnect_button', 'temperature_var', 'connect_button',
                     'heater_gear_var', 'collection_check', 'process_combo', 'start_button',
                     'stop_button', 'ntc_var', 'ntc_temperature_var', 'status', 'root', 'stop'):
            setattr(app, name, Mock())
        app.generation = 1
        app.ports = {'command': Mock()}
        app.controller = SimpleNamespace(active=True, pending=(0, 0), last_temperature=10,
                                         write_pending=(0, 0, 0), shutdown_queue=[(0, 0)],
                                         cooling_down=True, calibration='running', stop_confirmed=True,
                                         temperature=85, fresh=lambda now: now <= 110)
        app.recorder = Mock()
        app.manual_buttons = [Mock()]
        app.collect_only = False
        app.disconnecting = False
        app.history = deque(maxlen=1800)
        app.error_stats = TemperatureErrorStats()
        app.error_stats_var = Mock()
        app.board_temperature = 86
        app.board_temperature_received = 100
        return app

    def test_recent_history_excludes_queries(self):
        app = self.make_app()
        app.command_history = deque(maxlen=10)
        app.command_box = Mock()
        app.log = Mock()
        for command in (request_ntcs(), request_station3_snapshot(),
                        request_station1_board_temperature(), 'NB+status=?\r\n',
                        'MODBUS-TX:[03 03 00 3D 00 01 14 24]'):
            app.record_command(command, '', '已写入串口')
        self.assertFalse(app.command_history)
        app.command_box.insert.assert_not_called()
        for index in range(12):
            app.record_command(station3_write_register(4, index % 2), f'电机操作{index}', '已写入串口')
        self.assertEqual(len(app.command_history), 10)
        self.assertIn('电机操作11', app.command_history[0])
        self.assertIn('电机操作2', app.command_history[-1])
        self.assertTrue(is_operation_command('NB+VALVE=OPEN\r\n'))
        self.assertTrue(is_operation_command('NB+SOYMODE=0,0\r\n'))

    def test_scrollable_widgets_share_their_real_parent(self):
        container = Mock()
        ttk = SimpleNamespace(Frame=Mock(return_value=container), Scrollbar=Mock())
        namespace['ttk'] = ttk
        parent = Mock()
        widget = Mock()
        widget_type = Mock(return_value=widget)
        result = App.scrollable_text(parent, widget_type, height=6)
        widget_type.assert_called_once_with(container, height=6)
        self.assertIs(result, widget)
        widget.grid.assert_called_once_with(row=0, column=0, sticky='nsew')
        for call in ttk.Scrollbar.call_args_list:
            self.assertIs(call.args[0], container)

    def test_disconnect_feedback_and_release(self):
        app = self.make_app()
        port, recorder = app.ports['command'], app.recorder
        app.disconnect()
        app.stop.assert_called_once()
        app.status.assert_called_once()
        app.root.after.assert_called_once_with(3500, app.finish_disconnect)
        app.disconnect()
        app.stop.assert_called_once()
        app.finish_disconnect()
        port.close.assert_called_once()
        recorder.close.assert_called_once()
        self.assertFalse(app.ports)
        self.assertIsNone(app.recorder)
        self.assertIsNone(app.controller.write_pending)
        self.assertFalse(app.controller.cooling_down)
        self.assertFalse(app.disconnecting)
        app.real_value_var.set.assert_called_with('-- ℃')
        app.status.assert_called_with('已断开')

    def test_collect_only_disconnect_is_immediate(self):
        app = self.make_app()
        app.collect_only = True
        app.disconnect()
        app.stop.assert_not_called()
        app.root.after.assert_not_called()
        self.assertFalse(app.ports)

    def test_plot_alignment_stale_gaps_and_rate(self):
        app = self.make_app()
        app.controller.calibration = None
        app.sample_plot(100)
        self.assertEqual(app.history[-1], (100, 85, 86, 1))
        app.sample_plot(100.5)
        self.assertEqual(len(app.history), 1)
        app.sample_plot(104)
        self.assertEqual(app.history[-1], (104, 85, None, None))
        app.sample_plot(111)
        self.assertEqual(app.history[-1], (111, None, None, None))
        app.ports.clear()
        app.sample_plot(120)
        self.assertEqual(len(app.history), 3)
        self.assertEqual(app.error_stats.count, 1)
        self.assertEqual(app.error_stats.maximum, 1)
        self.assertIn('≤1℃覆盖率：100.0%', app.error_stats.summary())

    def test_calibration_samples_excluded_from_error_statistics(self):
        app = self.make_app()
        app.controller.calibration = None
        app.sample_plot(100)
        for now, stage in enumerate(('stopping', 'enabling', 'triggering', 'running'), 101):
            app.controller.calibration = stage
            app.board_temperature = 99
            app.board_temperature_received = now
            app.sample_plot(now)
        self.assertEqual(app.error_stats.count, 1)
        self.assertEqual(app.error_stats.maximum, 1)
        self.assertEqual(len(app.history), 5)  # Keep plotting calibration data.
        app.controller.calibration = None
        app.board_temperature = 87
        app.board_temperature_received = 105
        app.sample_plot(105)
        self.assertEqual(app.error_stats.count, 2)
        self.assertEqual(app.error_stats.maximum, 2)
        self.assertEqual(app.error_stats.within_one, 1)
        self.assertEqual(app.error_stats.within_two, 2)

    def test_three_series_and_separate_axis(self):
        app = self.make_app()
        app.canvas = Mock()
        app.canvas.winfo_width.return_value = 1000
        app.canvas.winfo_height.return_value = 200
        app.history.extend([(100, 85, 86, 1), (101, 85.5, 86, .5)])
        app.draw()
        curves = [call for call in app.canvas.create_line.call_args_list if len(call.args) == 4 and call.kwargs.get('width') == 2]
        # Legend lines plus one curve per signal.
        self.assertEqual(len(curves), 6)
        self.assertEqual({call.kwargs['fill'] for call in curves}, {'#147fb3', '#e28a24', '#8c57ba'})


if __name__ == '__main__':
    unittest.main()
