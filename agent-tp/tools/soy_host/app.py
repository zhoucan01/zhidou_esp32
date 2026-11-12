"""Windows serial host for station 3 Modbus control. Does not connect until requested."""
import queue
import math
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
from collections import deque
from pathlib import Path
from datetime import datetime
import serial
from serial.tools import list_ports
from control import (
    Controller, PROCESS_STAGES, MANUAL_COMMANDS, parse_temperature, describe_command, is_operation_command,
)
from settings import SettingsError, load_settings, save_settings
from telemetry import (
    Recorder, parse_ntcs, parse_ntc_temperatures, parse_station3_motor_rpm,
    parse_station1_board_temperature, request_ntcs, request_station3_snapshot,
    request_station1_board_temperature, TemperatureErrorStats,
)

MOTOR_RPM_POLL_INTERVAL = 0.1
MOTOR_RPM_REQUEST_TIMEOUT = 0.5
MOTOR_RPM_STALE_SECONDS = 0.5
BOARD_TEMPERATURE_POLL_INTERVAL = 1
BOARD_TEMPERATURE_REQUEST_TIMEOUT = 0.5
BOARD_TEMPERATURE_STALE_SECONDS = 3


class App:
    def __init__(self, root):
        self.root = root
        root.title("豆浆温度控制上位机")
        root.geometry("1120x820")
        root.minsize(940, 700)
        self.events = queue.Queue(maxsize=5000)
        self.ports = {}
        self.quit_reader = threading.Event()
        self.generation = 0
        self.disconnecting = False
        self.collect_only = False
        self.recorder = None
        self.ntcs = None
        self.ntc_temperatures = None
        self.ntc_time = ''
        self.ntc_temperature_time = ''
        self.real_time = ''
        self.ntc_received = 0
        self.ntc_temperature_received = 0
        self.motor_rpm = None
        self.motor_rpm_time = ''
        self.motor_rpm_received = 0
        self.motor_rpm_pending = False
        self.motor_rpm_request_at = 0
        self.board_temperature = None
        self.board_temperature_time = ''
        self.board_temperature_received = 0
        self.board_temperature_pending = False
        self.board_temperature_request_at = 0
        self.next_poll = 0
        self.next_motor_poll = 0
        self.next_board_temperature_poll = 0
        self.poll_pending = False
        self.history = deque(maxlen=1800)
        self.error_stats = TemperatureErrorStats()
        self.controller = Controller(self.send, self.status,
                                     on_temperature_timeout=self.prompt_temperature_timeout)
        self.status_var = tk.StringVar(value="未连接")
        self.temperature_var = tk.StringVar(
            value="真实温度：-- ℃    ESP32板级温度：-- ℃    温差：-- ℃")
        self.command_history = deque(maxlen=10)
        self.motor_var = tk.StringVar(value="电机：反馈未知")
        self.temperature_countdown_var = tk.StringVar(value="真实温度采集：等待有效数据")
        self.heater_gear_var = tk.StringVar(value="当前下加热档位：--")
        self.ntc_var = tk.StringVar(value="NTC1 ADC: --    NTC2 ADC: --    NTC3 ADC: --")
        self.ntc_temperature_var = tk.StringVar(
            value="NTC查表温度：NTC1 --    NTC2 --    NTC3 --")
        self.file_var = tk.StringVar(value="连接后自动记录到 agent-tp/data/captures/日期")
        self.same_port = tk.BooleanVar(value=False)
        self.collect_only_var = tk.BooleanVar(value=False)
        self.process_var = tk.StringVar(value="工艺1")
        self.build_ui()
        try:
            saved_settings = load_settings()
        except SettingsError as error:
            messagebox.showwarning("串口设置", str(error))
        else:
            self.command_port.set(saved_settings["command_port"])
            self.temperature_port.set(saved_settings["temperature_port"])
            self.command_baud.set(saved_settings["command_baud"])
            self.temperature_baud.set(saved_settings["temperature_baud"])
            self.same_port.set(saved_settings["same_port"])
        self.refresh()
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(100, self.poll)

    def build_ui(self):
        style = ttk.Style(self.root)
        style.theme_use('clam')
        style.configure('.', font=('Microsoft YaHei UI', 10))
        style.configure('TFrame', background='#edf2f7')
        style.configure('TLabel', background='#edf2f7', foreground='#233449')
        style.configure('Title.TLabel', font=('Microsoft YaHei UI', 19, 'bold'))
        style.configure('Muted.TLabel', foreground='#65768a')
        style.configure('Card.TFrame', background='white')
        style.configure('Card.TLabel', background='white', foreground='#65768a')
        style.configure('Value.TLabel', background='white', foreground='#173d61',
                        font=('Microsoft YaHei UI', 25, 'bold'))
        style.configure('TButton', padding=(12, 7))
        style.configure('Start.TButton', background='#146e9e', foreground='white')
        style.configure('Stop.TButton', background='#b73545', foreground='white')
        style.map('Start.TButton', background=[('disabled', '#d5dde5'), ('active', '#11577c')], foreground=[('disabled', '#708090')])
        style.map('Stop.TButton', background=[('disabled', '#d5dde5'), ('active', '#912a37')], foreground=[('disabled', '#708090')])
        style.configure('TNotebook.Tab', padding=(18, 8))
        frame = ttk.Frame(self.root, padding=16)
        frame.pack(fill='both', expand=True)
        header = ttk.Frame(frame)
        header.pack(fill='x', pady=(0, 12))
        ttk.Label(header, text='豆浆温控工作台', style='Title.TLabel').pack(side='left')
        self.stop_button = ttk.Button(header, text='停止加热与电机', style='Stop.TButton', command=self.stop)
        self.stop_button.pack(side='right')
        self.start_button = ttk.Button(header, text='开始自动加热', style='Start.TButton', command=self.start)
        self.start_button.pack(side='right', padx=10)
        self.connection_var = tk.StringVar(value='未连接')
        ttk.Label(header, textvariable=self.connection_var, style='Muted.TLabel').pack(side='right', padx=12)

        pages = ttk.Notebook(frame)
        pages.pack(fill='both', expand=True)
        overview = ttk.Frame(pages, padding=12)
        settings = ttk.Frame(pages, padding=18)
        pages.add(overview, text='实时监测')
        pages.add(settings, text='连接与手动控制')
        overview.columnconfigure(0, weight=1)
        overview.rowconfigure(2, weight=1)
        overview.rowconfigure(3, weight=1, minsize=200)
        metrics = ttk.Frame(overview)
        metrics.grid(row=0, column=0, sticky='ew')
        self.real_value_var = tk.StringVar(value='-- ℃')
        self.board_value_var = tk.StringVar(value='-- ℃')
        self.error_value_var = tk.StringVar(value='-- ℃')
        self.countdown_value_var = tk.StringVar(value='等待数据')
        self.countdown_hint_var = tk.StringVar(value='有效采样后开始计时')
        for index, (title, variable, hint) in enumerate((
                ('真实温度', self.real_value_var, '外部温度探头'),
                ('板级温度', self.board_value_var, 'ESP32温度反馈'),
                ('温差', self.error_value_var, '板级 − 真实温度'),
                ('采集倒计时', self.countdown_value_var, self.countdown_hint_var))):
            metrics.columnconfigure(index, weight=1, uniform='metric')
            card = ttk.Frame(metrics, style='Card.TFrame', padding=14)
            card.grid(row=0, column=index, sticky='nsew', padx=(0, 8) if index < 3 else 0)
            ttk.Label(card, text=title, style='Card.TLabel').pack(anchor='w')
            value_label = ttk.Label(card, textvariable=variable, style='Value.TLabel')
            value_label.pack(anchor='w', pady=5)
            if index == 3:
                self.countdown_label = value_label
            kwargs = {'textvariable': hint} if isinstance(hint, tk.StringVar) else {'text': hint}
            ttk.Label(card, style='Card.TLabel', **kwargs).pack(anchor='w')
        equipment = ttk.Frame(overview, padding=(0, 10))
        equipment.grid(row=1, column=0, sticky='ew')
        self.error_stats_var = tk.StringVar(value=self.error_stats.summary())
        ttk.Label(equipment, textvariable=self.error_stats_var, style='Muted.TLabel',
                  wraplength=1050, justify='left').pack(
            side='bottom', anchor='w', pady=(6, 0))
        self.heater_value_var = tk.StringVar(value='-- 档')
        self.motor_value_var = tk.StringVar(value='反馈未知')
        ttk.Label(equipment, text='下加热', style='Muted.TLabel').pack(side='left')
        ttk.Label(equipment, textvariable=self.heater_value_var).pack(side='left', padx=(8, 28))
        ttk.Label(equipment, text='电机', style='Muted.TLabel').pack(side='left')
        ttk.Label(equipment, textvariable=self.motor_value_var).pack(side='left', padx=8)
        plot = ttk.LabelFrame(overview, text='温度趋势 · 左轴温度 / 右轴温差（℃）', padding=6)
        plot.grid(row=2, column=0, sticky='nsew', pady=(0, 10))
        self.canvas = tk.Canvas(plot, height=150, background='white', highlightthickness=0)
        self.canvas.pack(fill='both', expand=True)
        self.canvas.bind('<Configure>', lambda event: self.draw())
        details = ttk.Notebook(overview)
        details.grid(row=3, column=0, sticky='nsew')
        recent = ttk.Frame(details, padding=8)
        logs = ttk.Frame(details, padding=8)
        sensors = ttk.Frame(details, padding=12)
        details.add(recent, text='最近10条指令')
        details.add(sensors, text='探头明细')
        details.add(logs, text='运行日志')
        ttk.Label(recent, text='仅显示操作指令（含重发）；查询记录见运行日志', style='Muted.TLabel').pack(anchor='w', pady=(0, 4))
        self.command_box = self.scrollable_text(recent, tk.Listbox, height=6, font=('Consolas', 10),
                                      background='white', foreground='#233449', relief='flat')
        self.log_box = self.scrollable_text(logs, tk.Text, height=6, state='disabled', font=('Consolas', 10),
                               background='white', foreground='#233449', relief='flat', wrap='none')
        self.sensor_table = ttk.Treeview(sensors, columns=('adc', 'temperature', 'error'), height=3)
        self.sensor_table.heading('#0', text='探头')
        for name, title in (('adc', '原始 ADC'), ('temperature', '查表温度 ℃'), ('error', '温差 ℃')):
            self.sensor_table.heading(name, text=title)
            self.sensor_table.column(name, width=180, anchor='center')
        self.sensor_table.column('#0', width=120)
        for channel in (1, 2, 3):
            self.sensor_table.insert('', 'end', iid=str(channel), text=f'NTC{channel}', values=('--', '--', '--'))
        self.sensor_table.pack(fill='x')
        ttk.Label(sensors, text='温差 = 查表温度 − 真实温度；过期数据以 -- 显示', style='Muted.TLabel').pack(anchor='w', pady=8)

        ttk.Label(settings, text='串口配置', style='Title.TLabel').grid(row=0, column=0, columnspan=6, sticky='w', pady=(0, 16))
        self.command_port = tk.StringVar(); self.temperature_port = tk.StringVar()
        self.command_baud = tk.StringVar(value='115200'); self.temperature_baud = tk.StringVar(value='115200')
        self.combos = []
        for row, (label, port, baud) in enumerate((('控制串口', self.command_port, self.command_baud), ('温度串口', self.temperature_port, self.temperature_baud)), start=1):
            ttk.Label(settings, text=label).grid(row=row, column=0, sticky='w', pady=8)
            combo = ttk.Combobox(settings, textvariable=port, width=18)
            combo.grid(row=row, column=1, padx=12); self.combos.append(combo)
            ttk.Label(settings, text='波特率').grid(row=row, column=2)
            ttk.Combobox(settings, textvariable=baud, values=(9600, 19200, 38400, 57600, 115200), width=12).grid(row=row, column=3, padx=12)
        ttk.Checkbutton(settings, text='控制和温度共用串口', variable=self.same_port).grid(row=3, column=0, columnspan=4, sticky='w', pady=8)
        self.collection_check = ttk.Checkbutton(settings, text='仅采集（连接前选择）', variable=self.collect_only_var)
        self.collection_check.grid(row=4, column=0, columnspan=4, sticky='w', pady=8)
        ttk.Label(settings, text='自动工艺').grid(row=5, column=0, sticky='w', pady=8)
        self.process_combo = ttk.Combobox(settings, textvariable=self.process_var, values=tuple(PROCESS_STAGES), state='readonly', width=18)
        self.process_combo.grid(row=5, column=1, padx=12)
        actions = ttk.Frame(settings)
        actions.grid(row=6, column=0, columnspan=4, sticky='w', pady=16)
        self.connect_button = ttk.Button(actions, text='连接', command=self.connect)
        self.connect_button.pack(side='left')
        ttk.Button(actions, text='刷新串口', command=self.refresh).pack(side='left', padx=10)
        self.disconnect_button = ttk.Button(actions, text='断开（先全停）', command=self.disconnect)
        self.disconnect_button.pack(side='left')
        manual = ttk.LabelFrame(settings, text='阀门与蠕动泵', padding=12)
        manual.grid(row=7, column=0, columnspan=6, sticky='ew', pady=12)
        self.manual_buttons = []
        for label, command in MANUAL_COMMANDS.items():
            button = ttk.Button(manual, text=label, command=lambda command=command, label=label: self.send_manual_command(command, label))
            button.pack(side='left', padx=4)
            self.manual_buttons.append(button)
        ttk.Label(settings, text='蠕动泵正反转固定90%；所有工艺最后一档至100℃后降温。', style='Muted.TLabel').grid(row=8, column=0, columnspan=6, sticky='w', pady=8)
        ttk.Label(settings, textvariable=self.file_var, style='Muted.TLabel', wraplength=850).grid(row=9, column=0, columnspan=6, sticky='w', pady=12)
        ttk.Separator(frame).pack(fill='x', pady=(10, 6))
        ttk.Label(frame, textvariable=self.status_var, wraplength=1040).pack(anchor='w')

    @staticmethod
    def scrollable_text(parent, widget_type, **options):
        container = ttk.Frame(parent)
        widget = widget_type(container, **options)
        container.pack(fill='both', expand=True)
        vertical = ttk.Scrollbar(container, orient='vertical', command=widget.yview)
        horizontal = ttk.Scrollbar(container, orient='horizontal', command=widget.xview)
        widget.configure(yscrollcommand=vertical.set, xscrollcommand=horizontal.set)
        widget.grid(row=0, column=0, sticky='nsew')
        vertical.grid(row=0, column=1, sticky='ns')
        horizontal.grid(row=1, column=0, sticky='ew')
        container.rowconfigure(0, weight=1)
        container.columnconfigure(0, weight=1)
        return widget

    def refresh(self):
        ports = [p.device for p in list_ports.comports()]
        for combo in self.combos: combo['values'] = ports

    def log(self, text):
        if self.recorder: self.recorder.raw(text)
        self.log_box.configure(state="normal")
        self.log_box.insert("end", time.strftime("%H:%M:%S ") + text + "\n")
        if int(self.log_box.index('end-1c').split('.')[0]) > 1500: self.log_box.delete('1.0', '300.0')
        self.log_box.see("end"); self.log_box.configure(state="disabled")

    def status(self, text):
        self.status_var.set(text); self.log(text)

    def send(self, command):
        if self.collect_only and command.strip().upper().startswith('NB+'):
            raise RuntimeError('仅采集模式禁止发送NB控制指令')
        port = self.ports.get('command')
        if not port or not port.is_open: raise RuntimeError("控制串口未连接，无法发送串口命令")
        description = describe_command(command)
        try:
            payload = command.encode('ascii')
            written = port.write(payload)
            if written != len(payload):
                raise RuntimeError(f"串口仅写入{written}/{len(payload)}字节")
        except Exception as error:
            self.record_command(command, description, "发送失败：" + str(error))
            raise
        self.record_command(command, description, "已写入串口")
        self.log("TX " + command.strip() + (" | 操作：" + description if description else ""))

    def record_command(self, command, description, result):
        if not is_operation_command(command):
            if result.startswith("发送失败"):
                self.log(result + " " + command.strip())
            return
        # __new__ 创建的无界面测试实例可仅验证串口发送。
        if not hasattr(self, 'command_history'):
            return
        entry = (datetime.now().isoformat(timespec='milliseconds') + " " + result
                 + " " + (description or command.strip()))
        self.command_history.appendleft(entry)
        self.command_box.delete(0, 'end')
        for row in self.command_history:
            self.command_box.insert('end', row)
        if result.startswith("发送失败"):
            self.log(entry)

    def reader(self, port, role, stop_event, generation):
        buffer = b''
        try:
            while not stop_event.is_set():
                chunk = port.read(256)
                if not chunk: continue
                buffer += chunk
                while b'\n' in buffer:
                    line, buffer = buffer.split(b'\n', 1)
                    self.events.put_nowait((generation, role, line.decode('utf-8', errors='replace').strip()))
                if len(buffer) > 4096: buffer = buffer[-4096:]
        except Exception as e:
            if not stop_event.is_set():
                try: self.events.put_nowait((generation, 'error', str(e)))
                except queue.Full: pass

    def connect(self):
        if self.ports: return
        try:
            self.collect_only = self.collect_only_var.get()
            process = self.process_var.get()
            if process not in PROCESS_STAGES:
                raise ValueError("请选择有效的工艺")
            cp = self.command_port.get().strip(); tp = cp if self.same_port.get() else self.temperature_port.get().strip()
            if not cp or not tp: raise ValueError("请选择控制串口和温度串口")
            if not self.same_port.get() and cp.upper() == tp.upper(): raise ValueError("同一串口请勾选共用串口")
            self.ports['command'] = serial.Serial(cp, int(self.command_baud.get()), timeout=.2, write_timeout=.5, dsrdtr=False, rtscts=False)
            if not self.same_port.get(): self.ports['temperature'] = serial.Serial(tp, int(self.temperature_baud.get()), timeout=.2)
            try:
                save_settings({
                    "command_port": cp,
                    "temperature_port": self.temperature_port.get().strip(),
                    "command_baud": self.command_baud.get().strip(),
                    "temperature_baud": self.temperature_baud.get().strip(),
                    "same_port": self.same_port.get(),
                })
            except SettingsError as error:
                self.status(str(error))
            self.recorder = Recorder(Path(__file__).resolve().parents[3] / 'agent-tp' /
                                     'data' / 'captures' / datetime.now().strftime('%Y-%m-%d'),
                                     process=process)
            self.file_var.set('CSV：' + str(self.recorder.csv_path))
            self.ntcs = None; self.ntc_time = self.real_time = ''; self.ntc_received = 0
            self.ntc_temperatures = None
            self.ntc_temperature_time = ''
            self.ntc_temperature_received = 0
            self.motor_rpm = None
            self.motor_rpm_time = ''
            self.motor_rpm_received = 0
            self.motor_rpm_pending = False
            self.motor_rpm_request_at = 0
            self.board_temperature = None
            self.board_temperature_time = ''
            self.board_temperature_received = 0
            self.board_temperature_pending = False
            self.board_temperature_request_at = 0
            self.next_poll = 0; self.poll_pending = False
            self.next_motor_poll = 0
            self.next_board_temperature_poll = 0
            self.quit_reader = threading.Event(); self.generation += 1
            self.controller = Controller(self.send, self.status, process=process,
                                         on_temperature_timeout=self.prompt_temperature_timeout)
            self.history.clear()
            self.error_stats = TemperatureErrorStats()
            self.error_stats_var.set(self.error_stats.summary())
            for role, port in self.ports.items():
                threading.Thread(target=self.reader, args=(port, 'both' if self.same_port.get() else role, self.quit_reader, self.generation), daemon=True).start()
            self.connect_button.configure(state='disabled')
            self.collection_check.configure(state='disabled')
            self.process_combo.configure(state='disabled')
            self.start_button.configure(state='disabled' if self.collect_only else 'normal')
            self.stop_button.configure(state='disabled' if self.collect_only else 'normal')
            for button in self.manual_buttons:
                button.configure(state='disabled' if self.collect_only else 'normal')
            self.status(f'{process}仅采集已连接：持续读取并保存，不发送NB控制' if self.collect_only else f'{process}已连接，等待温度。请勿同时运行固件自动流程')
        except Exception as e:
            self.release(); messagebox.showerror("连接失败", str(e))

    def prompt_temperature_timeout(self, controller, message):
        dialog = tk.Toplevel(self.root)
        dialog.title('真实温度超时：是否结束？')
        dialog.transient(self.root)
        ttk.Label(dialog, text=message + '\n\n等待选择或继续采集时，加热和电机维持当前阶段的开停节奏。'
                  '\n真实温度恢复前不切换温度阶段，无法依据真实温度判定100℃终点。',
                  wraplength=520, padding=16).pack()

        def finish(end=False):
            if (end and self.controller is controller and controller.active and
                    not controller.fresh(time.monotonic())):
                controller.stop(time.monotonic(), '真实温度超时，用户选择结束自动控制')
            dialog.destroy()

        buttons = ttk.Frame(dialog, padding=12)
        buttons.pack()
        ttk.Button(buttons, text='结束自动控制（继续记录）',
                   command=lambda: finish(True)).pack(side='left', padx=8)
        ttk.Button(buttons, text='维持状态，继续采集', command=finish).pack(side='left', padx=8)
        dialog.protocol('WM_DELETE_WINDOW', finish)

        def monitor():
            if not dialog.winfo_exists():
                return
            if (self.controller is not controller or not controller.active or
                    controller.fresh(time.monotonic())):
                dialog.destroy()
            else:
                self.root.after(250, monitor)

        self.root.after(250, monitor)

    def start(self):
        if self.collect_only: return
        if self.disconnecting: return
        try: self.controller.start(time.monotonic())
        except Exception as e: messagebox.showerror("无法启动", str(e))

    def stop(self):
        if self.collect_only: return
        try: self.controller.stop(time.monotonic())
        except Exception as e: self.controller.active = False; self.status("停机发送失败：" + str(e))

    def send_manual_command(self, command, label):
        try:
            self.send(command)
            self.status(f"已发送{label}指令")
        except Exception as e:
            messagebox.showerror("指令发送失败", str(e))

    def release(self):
        self.quit_reader.set(); self.generation += 1
        for port in self.ports.values(): port.close()
        self.ports.clear(); self.controller.active = False; self.controller.pending = None
        self.controller.last_temperature = None
        self.controller.write_pending = None
        self.controller.shutdown_queue = None
        self.controller.cooling_down = False
        self.controller.calibration = None
        self.real_value_var.set('-- ℃')
        self.board_value_var.set('-- ℃')
        self.error_value_var.set('-- ℃')
        self.connection_var.set('未连接')
        self.countdown_value_var.set('未连接')
        self.heater_value_var.set('-- 档')
        self.motor_value_var.set('反馈未知')
        for channel in (1, 2, 3):
            self.sensor_table.item(str(channel), values=('--', '--', '--'))
        self.disconnect_button.configure(state='normal')
        self.temperature_var.set(
            '真实温度：-- ℃    ESP32板级温度：-- ℃    温差：-- ℃')
        self.connect_button.configure(state='normal')
        self.heater_gear_var.set("当前下加热档位：--")
        self.collection_check.configure(state='normal')
        self.process_combo.configure(state='readonly')
        self.start_button.configure(state='normal'); self.stop_button.configure(state='normal')
        for button in self.manual_buttons: button.configure(state='normal')
        if self.recorder:
            self.recorder.raw('SESSION CLOSED'); self.recorder.close(); self.recorder = None
        self.ntcs = None
        self.ntc_temperatures = None
        self.motor_rpm = None
        self.motor_rpm_time = ''
        self.motor_rpm_received = 0
        self.motor_rpm_pending = False
        self.board_temperature = None
        self.board_temperature_time = ''
        self.board_temperature_received = 0
        self.board_temperature_pending = False
        self.ntc_var.set('NTC1 ADC: --    NTC2 ADC: --    NTC3 ADC: --')
        self.ntc_temperature_var.set(
            'NTC查表温度：NTC1 --    NTC2 --    NTC3 --')

    def disconnect(self):
        if self.disconnecting: return
        if not self.ports:
            self.status("当前未连接")
            return
        if self.collect_only:
            self.release(); self.status('仅采集已断开，未发送控制指令'); return
        self.disconnecting = True
        self.disconnect_button.configure(state="disabled")
        self.stop()
        self.status("正在断开：已请求全停，最多等待3.5秒确认…")
        self.root.after(3500, self.finish_disconnect)

    def finish_disconnect(self):
        confirmed = self.controller.stop_confirmed
        self.release(); self.status("已断开" if confirmed else "已断开；全停未确认，请检查设备")
        self.disconnecting = False

    def close(self):
        if self.collect_only:
            self.release(); self.root.destroy(); return
        if self.ports:
            self.disconnecting = True
            self.stop(); self.root.after(3500, self.finish_close)
        else: self.root.destroy()

    def finish_close(self):
        if not self.controller.stop_confirmed: messagebox.showwarning("全停未确认", "请检查设备并确认加热、电机已停止")
        self.release(); self.root.destroy()

    def poll(self):
        try:
            for _ in range(200):
                try: generation, role, line = self.events.get_nowait()
                except queue.Empty: break
                if generation != self.generation: continue
                now = time.monotonic()
                if role == 'error':
                    self.stop(); self.release(); self.status("串口异常：" + line); break
                self.log("RX " + role + " " + line)
                if role in ('both', 'command'):
                    self.controller.receive_ack(line, now)
                    self.controller.receive_modbus(line, now)
                    self.heater_gear_var.set(
                        f"当前下加热档位：{self.controller.heater_gear} 档")
                if role in ('both', 'command'):
                    ntcs = parse_ntcs(line)
                    if ntcs is not None:
                        self.ntcs = ntcs; self.ntc_received = now
                        self.ntc_time = datetime.now().isoformat(timespec='milliseconds')
                        self.poll_pending = False
                        self.ntc_var.set('    '.join(f'NTC{i} ADC: {value}' for i, value in enumerate(ntcs, 1)))
                        self.log(f'采样：最近有效真实温度={self.controller.temperature}℃（接收时间={self.real_time or "尚无"}），NTC1/2/3 ADC={ntcs}')
                        self.record_sample('NTC', now)
                    ntc_temperatures = parse_ntc_temperatures(line)
                    if ntc_temperatures is not None:
                        self.ntc_temperatures = ntc_temperatures
                        self.ntc_temperature_received = now
                        self.ntc_temperature_time = datetime.now().isoformat(
                            timespec='milliseconds')
                        self.record_sample('NTC查表温度', now)
                    motor_rpm = parse_station3_motor_rpm(line)
                    if motor_rpm is not None:
                        self.motor_rpm = motor_rpm
                        self.motor_rpm_received = now
                        self.motor_rpm_time = datetime.now().isoformat(
                            timespec='milliseconds')
                        self.motor_rpm_pending = False
                        self.record_sample('站3电机转速', now)
                    board_temperature = parse_station1_board_temperature(line)
                    if board_temperature is not None:
                        self.board_temperature = board_temperature
                        self.board_temperature_received = now
                        self.board_temperature_time = datetime.now().isoformat(
                            timespec='milliseconds')
                        reference = (
                            self.controller.temperature
                            if self.controller.fresh(now) else None)
                        error = (
                            f'{board_temperature - reference:+.2f}℃'
                            if reference is not None else '--')
                        self.log(
                            f'采样：ESP32板级温度={board_temperature:.2f}℃，'
                            f'真实温度={reference if reference is not None else "--"}℃，'
                            f'温差={error}')
                        self.board_temperature_pending = False
                        self.record_sample('ESP32板级温度', now)
                if role in ('both', 'temperature'):
                    value = parse_temperature(line)
                    if value is None and ('T=' in line.upper().replace(' ', '') or 'TEMP' in line.upper()):
                        self.log('真实温度未更新：未解析到0～150℃有效值；倒计时继续')
                    if value is not None:
                        self.controller.receive_temperature(value, now)
                        self.real_time = datetime.now().isoformat(timespec='milliseconds')
                        self.record_sample('真实温度', now)
                self.update_ntc_display(now)
            if not self.collect_only: self.controller.tick(time.monotonic())
            now = time.monotonic()
            if self.ports and not self.disconnecting and now >= self.next_poll:
                self.send(request_ntcs())
                self.next_poll = now + 2 if self.poll_pending else now + 1
                self.poll_pending = True
            if self.ports and not self.disconnecting:
                if (self.motor_rpm_pending and
                        now - self.motor_rpm_request_at >=
                        MOTOR_RPM_REQUEST_TIMEOUT):
                    self.motor_rpm_pending = False
                if now >= self.next_motor_poll and not self.motor_rpm_pending:
                    self.send(request_station3_snapshot())
                    self.motor_rpm_pending = True
                    self.motor_rpm_request_at = now
                if (self.board_temperature_pending and
                        now - self.board_temperature_request_at >=
                        BOARD_TEMPERATURE_REQUEST_TIMEOUT):
                    self.board_temperature_pending = False
                if (now >= self.next_board_temperature_poll and
                        not self.board_temperature_pending):
                    self.send(request_station1_board_temperature())
                    self.board_temperature_pending = True
                    self.board_temperature_request_at = now
                    self.next_board_temperature_poll = (
                        now + BOARD_TEMPERATURE_POLL_INTERVAL)
                if now >= self.next_motor_poll:
                    self.next_motor_poll = now + MOTOR_RPM_POLL_INTERVAL
            self.update_ntc_display(now)
            self.update_temperature_display(now)
            rpm = self.motor_rpm if self.ports and now - self.motor_rpm_received <= MOTOR_RPM_STALE_SECONDS else None
            pending = self.controller.write_pending
            target = f"；待确认 {pending[1]} 档" if pending and pending[0] == 4 else ""
            feedback = f"{rpm} RPM（{'转动' if rpm else '静止'}）" if rpm is not None else "未知/已过期"
            gear = str(self.controller.motor_confirmed_gear) if self.ports and self.controller.motor_confirmed_gear is not None else "未知"
            self.motor_var.set(f"电机写入已确认档位：{gear}；实测：{feedback}{target}")
            self.sample_plot(now)
            self.draw()
        except Exception as e:
            self.controller.active = False
            self.status("控制异常：" + str(e))
            if self.collect_only:
                self.release(); self.status('采集异常，已断开；未发送控制指令')
            else:
                try: self.send('NB+SOYMODE=0,0\r\n')
                except Exception: self.status("无法发送全停，请检查设备")
        self.root.after(100, self.poll)

    def record_sample(self, source, now):
        if not self.recorder: return
        temperature = self.controller.temperature if self.controller.fresh(now) else None
        ntcs = self.ntcs if now - self.ntc_received <= 3 else None
        ntc_temperatures = (
            self.ntc_temperatures
            if now - self.ntc_temperature_received <= 3 else None)
        motor_rpm = (
            self.motor_rpm
            if now - self.motor_rpm_received <= MOTOR_RPM_STALE_SECONDS
            else None)
        board_temperature = (
            self.board_temperature
            if now - self.board_temperature_received <=
            BOARD_TEMPERATURE_STALE_SECONDS else None)
        self.recorder.sample(source, temperature, self.real_time if temperature is not None else '',
                             ntcs, self.ntc_time if ntcs is not None else '',
                             self.controller.mode, ntc_temperatures,
                             self.ntc_temperature_time
                             if ntc_temperatures is not None else '',
                             motor_rpm, self.motor_rpm_time
                             if motor_rpm is not None else '',
                             board_temperature,
                             self.board_temperature_time
                             if board_temperature is not None else '')

    def update_temperature_display(self, now):
        remaining = self.controller.temperature_remaining(now)
        if not self.ports:
            countdown = "真实温度采集：未连接"
        elif remaining is None:
            countdown = "真实温度采集：等待有效数据"
        else:
            age = max(0.0, now - self.controller.last_temperature)
            state = "已超时" if not self.controller.fresh(now) else f"超时倒计时 {remaining:.1f}秒"
            countdown = (f"真实温度采集：{state}；限时 {self.controller.temperature_timeout():g}秒；"
                         f"最后有效 {self.controller.temperature:.1f}℃，{age:.1f}秒前")
        self.temperature_countdown_var.set(countdown)
        self.connection_var.set('仅采集' if self.ports and self.collect_only else '已连接' if self.ports else '未连接')
        self.countdown_value_var.set('未连接' if not self.ports else '等待数据' if remaining is None else '已超时' if not self.controller.fresh(now) else f'{remaining:.1f} s')
        self.countdown_hint_var.set(f'全程限时 {self.controller.temperature_timeout():g} 秒')
        self.countdown_label.configure(foreground='#b73545' if self.ports and remaining is not None and remaining <= 2 else '#173d61')
        self.heater_value_var.set(f'{self.controller.heater_gear} 档' if self.ports and not self.collect_only else '-- 档')
        rpm = self.motor_rpm if self.ports and now - self.motor_rpm_received <= MOTOR_RPM_STALE_SECONDS else None
        gear = self.controller.motor_confirmed_gear
        pending = self.controller.write_pending
        motor_text = f'{gear}档已确认' if self.ports and gear is not None else '档位未知'
        motor_text += f' · {rpm} RPM' if rpm is not None else ' · 转速未知'
        if pending and pending[0] == 4:
            motor_text += f' · 待确认{pending[1]}档'
        self.motor_value_var.set(motor_text)
        real_temperature = (
            self.controller.temperature
            if self.ports and self.controller.fresh(now) else None)
        board_temperature = (
            self.board_temperature
            if self.ports and now - self.board_temperature_received <=
            BOARD_TEMPERATURE_STALE_SECONDS else None)
        real_text = (
            f'{real_temperature:.1f} ℃'
            if real_temperature is not None else '-- ℃')
        board_text = (
            f'{board_temperature:.2f} ℃'
            if board_temperature is not None else '-- ℃')
        error_text = (
            f'{board_temperature - real_temperature:+.2f} ℃'
            if board_temperature is not None and real_temperature is not None
            else '-- ℃')
        self.real_value_var.set(real_text)
        self.board_value_var.set(board_text)
        self.error_value_var.set(error_text)
        for channel in (1, 2, 3):
            adc = self.ntcs[channel-1] if self.ports and self.ntcs is not None and now-self.ntc_received <= 3 else '--'
            temp = self.ntc_temperatures[channel-1] if self.ports and self.ntc_temperatures is not None and now-self.ntc_temperature_received <= 3 else None
            error = f'{temp-real_temperature:+.2f}' if temp is not None and real_temperature is not None else '--'
            self.sensor_table.item(str(channel), values=(adc, f'{temp:.2f}' if temp is not None else '--', error))
        self.temperature_var.set(
            f'真实温度：{real_text}    ESP32板级温度：{board_text}    '
            f'温差：{error_text}')

    def update_ntc_display(self, now):
        if self.ntcs is None:
            self.ntc_var.set('NTC1 ADC: --    NTC2 ADC: --    NTC3 ADC: --')
        elif now - self.ntc_received > 3:
            self.ntc_var.set('NTC数据超过3秒未更新')

        if (self.ntc_temperatures is None or
                now - self.ntc_temperature_received > 3):
            self.ntc_temperature_var.set(
                'NTC查表温度：NTC1 --    NTC2 --    NTC3 --')
            return

        reference = (
            self.controller.temperature
            if self.controller.fresh(now) else None)
        values = []
        for channel, value in enumerate(self.ntc_temperatures, 1):
            error = (f'{value - reference:+.2f}℃'
                     if reference is not None else '--')
            values.append(
                f'NTC{channel}: {value:.2f}℃（误差 {error}）')
        self.ntc_temperature_var.set('NTC查表温度：' + '    '.join(values))

    def sample_plot(self, now):
        if not self.ports or (self.history and now - self.history[-1][0] < 1):
            return
        real = self.controller.temperature if self.controller.fresh(now) else None
        board = (self.board_temperature if self.board_temperature is not None and
                 now - self.board_temperature_received <= BOARD_TEMPERATURE_STALE_SECONDS else None)
        error = board - real if board is not None and real is not None else None
        self.history.append((now, real, board, error))
        if self.controller.calibration is None:
            self.error_stats.add(error, real, board)
        self.error_stats_var.set(self.error_stats.summary())

    def draw(self):
        canvas = self.canvas
        canvas.delete('all')
        w, h = max(canvas.winfo_width(), 200), max(canvas.winfo_height(), 120)
        left, right, top, bottom = 42, w - 48, 30, h - 25
        series = (('真实温度', '#147fb3', 1), ('板级温度', '#e28a24', 2), ('温差', '#8c57ba', 3))
        for index, (name, color, _) in enumerate(series):
            x = left + index * 120
            canvas.create_line(x, 13, x+18, 13, fill=color, width=2)
            canvas.create_text(x+24, 13, text=name, anchor='w', fill=color)
        for temperature in (0, 25, 50, 75, 100):
            y = bottom - temperature / 110 * (bottom-top)
            canvas.create_line(left, y, right, y, fill='#e6edf3')
            canvas.create_text(left-8, y, text=str(temperature), anchor='e', fill='#65768a')
        values = list(self.history)
        errors = [abs(row[3]) for row in values if row[3] is not None]
        error_limit = max(1, math.ceil(max(errors, default=1)))
        for error in (-error_limit, 0, error_limit):
            y = bottom - (error+error_limit)/(2*error_limit)*(bottom-top)
            canvas.create_text(right+6, y, text=f'{error:+g}', anchor='w', fill='#8c57ba')
        if not values:
            canvas.create_text((left+right)/2, (top+bottom)/2, text='连接后显示温度与温差曲线', fill='#65768a')
            return
        start, end = values[0][0], values[-1][0]
        span = max(1, end-start)
        for _, color, column in series:
            segment, previous = [], None
            def flush():
                if len(segment) >= 4:
                    canvas.create_line(*segment, fill=color, width=2)
                elif segment:
                    x, y = segment
                    canvas.create_oval(x-2, y-2, x+2, y+2, fill=color, outline=color)
            for row in values:
                value = row[column]
                if value is None or (previous is not None and row[0]-previous > 2.5):
                    flush()
                    segment = []
                if value is not None:
                    x = left+(row[0]-start)/span*(right-left)
                    fraction = (value+error_limit)/(2*error_limit) if column == 3 else value/110
                    y = bottom-fraction*(bottom-top)
                    segment.extend((x, y))
                previous = row[0]
            flush()
        canvas.create_text(left, h-9, text=f'-{end-start:.0f}秒', anchor='w', fill='#65768a')
        canvas.create_text(right, h-9, text='最新', anchor='e', fill='#65768a')


if __name__ == '__main__':
    root=tk.Tk();App(root);root.mainloop()
