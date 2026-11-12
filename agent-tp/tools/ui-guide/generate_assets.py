"""Render current LVGL pages and export upright PNGs using only Python stdlib.

Run test_plantmilk_ui.py --ui-validation-mode 0 with the same compiler first.
No firmware source or configuration is changed. Outputs stay in build/ or agent-tp/.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / 'build/ccache/plantmilk-host/mode-0'
OUT = ROOT / 'build/ccache/ui-guide'
REPORT = ROOT / 'agent-tp/reports/ui-operation-guide'


def write_png(path, width, height, pixels):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def upright(ppm):
    # Native buffer is 320x480. Rotate counterclockwise for readable 480x320 UI.
    header = b'P6\n320 480\n255\n'
    content = ppm.read_bytes()
    assert content.startswith(header)
    source = content[len(header):]
    assert len(source) == 320 * 480 * 3
    target = bytearray(len(source))
    for y in range(320):
        for x in range(480):
            src = (x * 320 + 319 - y) * 3
            dst = (y * 480 + x) * 3
            target[dst:dst + 3] = source[src:src + 3]
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    images = REPORT / 'images'
    images.mkdir(parents=True, exist_ok=True)
    frames = OUT / 'frames'
    frames.mkdir(exist_ok=True)
    source = ROOT / 'agent-tp/tools/ui-guide/render_pages.c'
    obj = OUT / 'render.o'
    flags = ['-std=c99', '-O0', '-g', '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_LVGL_H_INCLUDE_SIMPLE',
             '-DMYLVGL_UI_VALIDATION_MODE=0']
    for directory in ['agent-tp/tests/firmware/plantmilk_host', 'components/lvgl', 'function/UI',
                      'function/LZ4', 'components/myLvgl/include', 'components/MyEncoder/include']:
        flags.append('-I' + str(ROOT / directory))
    flags += ['-I' + str(BASE), '-include', str(BASE / 'host_sdkconfig.h')]
    subprocess.run([args.cc, *flags, '-c', str(source), '-o', str(obj)], check=True)
    original = ROOT / 'agent-tp/tests/firmware/test_plantmilk_ui.c'
    original_name = hashlib.sha256(str(original).encode()).hexdigest()[:20] + '.o'
    objects = [line for line in (BASE / 'objects.rsp').read_text().splitlines()
               if Path(line.strip('"')).name != original_name]
    objects.append('"' + str(obj).replace('\\', '/') + '"')
    rsp = OUT / 'objects.rsp'
    rsp.write_text('\n'.join(objects), encoding='utf-8')
    exe = OUT / 'render.exe'
    subprocess.run([args.cc, '@' + str(rsp), '-static', '-lm', '-o', str(exe)], check=True)
    result = subprocess.run([str(exe), str(frames)], capture_output=True, text=True, timeout=120)
    (REPORT / 'render.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    pages = sorted(frames.glob('*.ppm'))
    rendered = []
    for ppm in pages:
        pixels = upright(ppm)
        write_png(images / (ppm.stem + '.png'), 480, 320, pixels)
        rendered.append((ppm.stem, pixels))
    # Overview sheets use the same pixels, nearest-neighbor at half size.
    for start in range(0, len(rendered), 16):
        group = rendered[start:start + 16]
        width, height = 4 * 252, ((len(group) + 3) // 4) * 180
        sheet = bytearray(b'\xE8\xEC\xF0' * width * height)
        for index, (_, pixels) in enumerate(group):
            ox, oy = (index % 4) * 252 + 6, (index // 4) * 180 + 6
            for y in range(160):
                for x in range(240):
                    dst = ((oy + y) * width + ox + x) * 3
                    src = ((y * 2) * 480 + x * 2) * 3
                    sheet[dst:dst + 3] = pixels[src:src + 3]
        write_png(REPORT / f'overview-{start // 16 + 1}.png', width, height, sheet)
    (REPORT / 'image-index.txt').write_text('\n'.join(name for name, _ in rendered) + '\n', encoding='utf-8')
    print(result.stdout.splitlines()[-1])
    print(f'Exported {len(pages)} upright PNGs: {images}')


if __name__ == '__main__':
    main()
