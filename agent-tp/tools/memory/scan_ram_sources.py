"""Inventory current project allocation sites, memory configuration and ELF statics.

This is a source inventory, not proof of runtime reachability or of a leak.
Reports go to agent-tp; no firmware/configuration file is modified.
"""
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
REPORT = ROOT/'agent-tp/reports/ram-audit-2026-11-06'
CALLS = re.compile(r'\b(?:heap_caps_(?:malloc|calloc|realloc)|malloc|calloc|realloc|free|'
                   r'xTaskCreate\w*|xQueueCreate\w*|xEventGroupCreate\w*|xSemaphoreCreate\w*|'
                   r'pvPortMalloc|lv_mem_(?:alloc|realloc|free)|strdup|esp_http_client_init|'
                   r'httpd_start|nimble_port_init)\s*\(')


def main():
    REPORT.mkdir(parents=True, exist_ok=True)
    compiled = {Path(row['file']).resolve() for row in
                json.loads((ROOT/'build/compile_commands.json').read_text(encoding='utf-8'))}
    rows, sources = [], []
    for base in ['main', 'task', 'function', 'hardware', 'components']:
        for path in (ROOT/base).rglob('*'):
            if path.suffix.lower() not in {'.c', '.h', '.cpp', '.hpp'}:
                continue
            if path.name.startswith(('ui_img_', 'ui_font_')):
                continue
            sources.append(path)
            text = path.read_text(encoding='utf-8', errors='replace')
            for line_no, line in enumerate(text.splitlines(), 1):
                if CALLS.search(line) and not line.lstrip().startswith(('*', '//')):
                    rows.append(dict(file=path.relative_to(ROOT).as_posix(), line=line_no,
                                     compiled_translation_unit=path.resolve() in compiled,
                                     source=line.strip()))
    with (REPORT/'allocation-sites.csv').open('w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['file', 'line', 'compiled_translation_unit', 'source'])
        writer.writeheader()
        writer.writerows(rows)
    sdk = (ROOT/'sdkconfig').read_text(encoding='utf-8')
    config = {key: value for key, value in re.findall(r'^(CONFIG_[A-Z0-9_]+)=(.*)$', sdk, re.M)
              if re.search('SPIRAM|LV_|BT_NIMBLE|ESP_WIFI|MBEDTLS|LWIP|STACK|HEAP|WDT', key)}
    map_text = (ROOT/'build/EVT_ESP32-S3.map').read_text(encoding='utf-8')
    sections = []
    for name, address, size in re.findall(r'^(\.[^\s]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)', map_text, re.M):
        if name.startswith(('.dram0.', '.iram0.', '.ext_ram.')):
            sections.append(dict(name=name, address=address, bytes=int(size, 16)))
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    branch = subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip()
    report = dict(branch=branch, commit=head, scanned_non_asset_sources=len(sources),
                  compiled_project_translation_units=sum(p.is_relative_to(ROOT) for p in compiled),
                  allocation_candidate_lines=len(rows), config=config, sections=sections,
                  hashes={name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
                          for name in ['sdkconfig', 'build/EVT_ESP32-S3.bin', 'build/EVT_ESP32-S3.map']})
    (REPORT/'inventory.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('Scanned source/header files:', len(sources))
    print('Allocation candidate lines:', len(rows))
    print('Compiled project translation units:', report['compiled_project_translation_units'])
    print('RAM sections:', sections)


if __name__ == '__main__':
    main()
