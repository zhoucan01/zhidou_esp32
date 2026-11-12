"""Read station-1 flow/heap diagnostics using FC04 only; never starts a process."""
import argparse
from datetime import datetime
import csv
from pathlib import Path
import re
import time
import serial

ROOT = Path(__file__).resolve().parents[3]


def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return crc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--seconds', type=float, default=30)
    args = parser.parse_args()
    directory = ROOT/'agent-tp/data/captures'/datetime.now().strftime('%Y-%m-%d')
    directory.mkdir(parents=True, exist_ok=True)
    stem = datetime.now().strftime('%H%M%S-ram-audit')
    payload = bytes.fromhex('01 04 00 12 00 0A')
    payload += crc16(payload).to_bytes(2, 'little')
    request = ('MODBUS-TX:['+' '.join(f'{b:02X}' for b in payload)+']\r\n').encode()
    port = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=1)
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()
    samples = []
    try:
        with (directory/(stem+'.csv')).open('w', newline='', encoding='utf-8') as csvfile, \
             (directory/(stem+'-serial.txt')).open('wb') as raw:
            writer = csv.DictWriter(csvfile, fieldnames=['seconds', 'flow', 'order', 'psram_free', 'internal_free', 'frames'])
            writer.writeheader()
            started = next_poll = time.monotonic()
            buffer = b''
            while time.monotonic() - started < args.seconds:
                now = time.monotonic()
                if now >= next_poll:
                    port.write(request)
                    next_poll = now + 1
                chunk = port.read(4096)
                raw.write(chunk)
                buffer = (buffer + chunk)[-16384:]
                for match in list(re.finditer(rb'MODBUS-RX:\[([0-9A-Fa-f ]+)\]', buffer)):
                    try:
                        data = bytes.fromhex(match.group(1).decode())
                        if data[:3] != bytes([1, 4, 20]) or len(data) != 25:
                            continue
                        if crc16(data[:-2]) != int.from_bytes(data[-2:], 'little'):
                            continue
                        words = [int.from_bytes(data[i:i+2], 'big') for i in range(3,23,2)]
                        row = dict(seconds=round(now-started, 3), flow=words[0], order=words[1],
                                   psram_free=(words[2]<<16)|words[3],
                                   internal_free=(words[4]<<16)|words[5],
                                   frames=(words[6]<<16)|words[7])
                        writer.writerow(row)
                        csvfile.flush()
                        samples.append(row)
                    finally:
                        pass
                if b'MODBUS-RX:[' in buffer:
                    last_close = buffer.rfind(b']')
                    if last_close >= 0:
                        buffer = buffer[last_close+1:]
            print('Capture:', directory/(stem+'.csv'))
            print('Samples:', len(samples))
            if samples:
                for key in ['psram_free', 'internal_free']:
                    print(key, 'min=', min(r[key] for r in samples), 'max=', max(r[key] for r in samples))
                print('Flows:', sorted({r['flow'] for r in samples}))
    finally:
        port.close()


if __name__ == '__main__':
    main()
