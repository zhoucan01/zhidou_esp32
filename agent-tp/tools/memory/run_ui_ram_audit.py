"""Link an instrumented UI audit against existing real LVGL host objects.

Run the existing test_plantmilk_ui.py with the same compiler first.
The audit main, LZ4 decoder and an allocator copy are rebuilt; firmware stays unchanged.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / 'build/ccache/plantmilk-host'
OUT = ROOT / 'build/ccache/ui-ram-audit'
REPORT = ROOT / 'agent-tp/reports/ram-audit-2026-11-06'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    expected_config = []
    for line in (ROOT/'sdkconfig').read_text(encoding='utf-8').splitlines():
        if line.startswith('CONFIG_LV_') and not line.startswith('CONFIG_LV_CONF_SKIP='):
            key, value = line.split('=', 1)
            expected_config.append('#define '+key+' '+('1' if value == 'y' else value))
    if (BASE/'host_sdkconfig.h').read_text(encoding='utf-8') != '\n'.join(expected_config)+'\n':
        raise SystemExit('Host objects use a different Kconfig; rerun test_plantmilk_ui.py first.')
    OUT.mkdir(parents=True, exist_ok=True)
    REPORT.mkdir(parents=True, exist_ok=True)
    headers = OUT / 'include'
    headers.mkdir(exist_ok=True)
    (headers/'esp_heap_caps.h').write_text('''#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t size, unsigned caps);
void heap_caps_free(void *ptr);
''', encoding='utf-8')
    flags = ['-std=c99', '-O0', '-g', '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_LVGL_H_INCLUDE_SIMPLE',
             '-I'+str(headers)]
    for directory in ['agent-tp/tests/firmware/plantmilk_host', 'components/lvgl', 'function/UI',
                      'function/LZ4', 'components/myLvgl/include', 'components/MyEncoder/include']:
        flags.append('-I'+str(ROOT/directory))
    flags += ['-I'+str(BASE), '-include', str(BASE/'host_sdkconfig.h')]
    audit = ROOT/'agent-tp/tests/firmware/test_ui_ram_pressure.c'
    decoder = ROOT/'function/UI/ui_lz4_decoder.c'
    memory = ROOT/'components/lvgl/src/misc/lv_mem.c'
    instrumented_memory = OUT/'lv_mem_audit.c'
    memory_text = memory.read_text(encoding='utf-8')
    assert memory_text.count('    return alloc;') == 1
    assert memory_text.count('    return new_p;') == 1
    memory_text = 'extern void audit_observe_pool(void *result);\n' + memory_text
    memory_text = memory_text.replace('    return alloc;', '    audit_observe_pool(alloc);\n    return alloc;')
    memory_text = memory_text.replace('    return new_p;', '    audit_observe_pool(new_p);\n    return new_p;')
    instrumented_memory.write_text(memory_text, encoding='utf-8')
    flags.append('-I'+str(memory.parent))
    audit_obj, decoder_obj, memory_obj = OUT/'audit.o', OUT/'decoder.o', OUT/'memory.o'
    for source, obj in [(audit, audit_obj), (decoder, decoder_obj), (instrumented_memory, memory_obj)]:
        subprocess.run([args.cc, *flags, '-c', str(source), '-o', str(obj)], check=True)
    replaced = set()
    for source in [ROOT/'agent-tp/tests/firmware/test_plantmilk_ui.c', decoder, memory]:
        replaced.add(hashlib.sha256(str(source).encode()).hexdigest()[:20]+'.o')
    objects = [line for line in (BASE/'objects.rsp').read_text().splitlines()
               if Path(line.strip('"')).name not in replaced]
    assert len(objects) > 400
    objects += ['"'+str(path).replace('\\', '/')+'"' for path in [audit_obj, decoder_obj, memory_obj]]
    rsp = OUT/'objects.rsp'
    rsp.write_text('\n'.join(objects), encoding='utf-8')
    exe = OUT/('audit.exe' if os.name == 'nt' else 'audit')
    subprocess.run([args.cc, '@'+str(rsp), '-o', str(exe), '-static', '-lm'], check=True)
    frames = OUT/'frames'
    frames.mkdir(exist_ok=True)
    result = subprocess.run([str(exe), str(frames), str(REPORT/'ui-scenes.csv')],
                            capture_output=True, text=True, timeout=180)
    (REPORT/'ui-audit.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    print('\n'.join(line for line in result.stdout.splitlines() if line.startswith('AUDIT_')))
    if result.returncode:
        raise SystemExit(result.stderr or result.stdout[-2000:])


if __name__ == '__main__':
    main()
