"""不依赖开发板，编译并运行真实 LVGL 和所有逻辑页面。

Usage: python agent-tp/tests/firmware/test_plantmilk_ui.py --cc /path/to/gcc
目标文件、链接参数、可执行文件和页面图片统一放在 build/ccache/。
实际 UART 通信、RTOS 调度与硬件工艺仍需在设备上验证。
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/ccache/plantmilk-host'

def main():
    global OUT
    parser=argparse.ArgumentParser()
    parser.add_argument('--cc',default=shutil.which('gcc'))
    parser.add_argument('--ui-validation-mode',type=int,choices=(0,1))
    args=parser.parse_args()
    if not args.cc:raise SystemExit('A host GCC compiler is required; pass --cc.')
    if args.ui_validation_mode is not None:
        OUT=OUT/f'mode-{args.ui_validation_mode}'
    OUT.mkdir(parents=True,exist_ok=True)
    # ESP-IDF uses Kconfig (CONFIG_LV_CONF_SKIP=y), not main/lv_conf.h.
    # Import every enabled LVGL option, but keep the host diagnostics header.
    sdkconfig=ROOT/'sdkconfig'
    config_lines=[]
    for line in sdkconfig.read_text(encoding='utf-8').splitlines():
        if line.startswith('CONFIG_LV_') and not line.startswith('CONFIG_LV_CONF_SKIP='):
            key,value=line.split('=',1)
            config_lines.append('#define '+key+' '+('1' if value=='y' else value))
    host_config=OUT/'host_sdkconfig.h'
    host_config.write_text('\n'.join(config_lines)+'\n',encoding='utf-8')
    frames=OUT/'frames'
    frames.mkdir(exist_ok=True)
    datacenter=ROOT/'task/DataCenter/DataCenter.c'
    datacenter_text=datacenter.read_text(encoding='utf-8')
    snapshot_impl=datacenter_text[datacenter_text.index('bool DataCenterGetSensorSnapshot('):
                                    datacenter_text.index('bool ModbusCopyCachedInputRegisters(')]
    snapshot_include=OUT/'sensor_snapshot_impl.inc'
    if not snapshot_include.exists() or snapshot_include.read_text(encoding='utf-8') != snapshot_impl:
        snapshot_include.write_text(snapshot_impl,encoding='utf-8')
    ui=ROOT/'function/UI'
    lvgl=ROOT/'components/lvgl'
    sources=list((lvgl/'src').rglob('*.c'))
    sources += [ui/'ui.c', ui/'ui_lz4_decoder.c']
    sources+=list((ui/'Tofu').glob('*.c'))+list((ui/'PlantMilk').glob('*.c'))
    sources+=list((ui/'Clean').glob('*.c'))
    sources+=list((ui/'Font').glob('*.c'))+list((ui/'images').glob('*.c'))+list((ui/'images_lz4').glob('*.c'))
    sources += [ROOT/'function/LZ4/lz4.c', ROOT/'components/myLvgl/plantmilk_ui_flow.c', ROOT/'components/myLvgl/clean_ui_flow.c', ROOT/'agent-tp/tests/firmware/test_plantmilk_ui.c']
    sources += [ROOT/'agent-tp/tests/firmware/test_sensor_snapshot.c']
    flags=['-std=c99','-O0','-g','-DLV_CONF_INCLUDE_SIMPLE','-DLV_LVGL_H_INCLUDE_SIMPLE',
           '-I'+str(ROOT/'agent-tp/tests/firmware/plantmilk_host'),'-I'+str(lvgl),'-I'+str(ui),
           '-I'+str(ROOT/'function/LZ4'),'-I'+str(ROOT/'components/myLvgl/include'),
           '-I'+str(ROOT/'components/MyEncoder/include')]
    flags.append('-I'+str(OUT))
    flags += ['-include',str(host_config)]
    if args.ui_validation_mode is not None:
        flags.append(f'-DMYLVGL_UI_VALIDATION_MODE={args.ui_validation_mode}')
    def compile_one(source):
        obj=OUT/(hashlib.sha256(str(source).encode()).hexdigest()[:20]+'.o')
        # 头文件发生变化时，同步重新编译缓存的桌面目标文件。
        newest=max(source.stat().st_mtime_ns, datacenter.stat().st_mtime_ns,
                   Path(args.cc).stat().st_mtime_ns,
                   (ROOT/'main/lv_conf.h').stat().st_mtime_ns,
                   sdkconfig.stat().st_mtime_ns,
                   Path(__file__).stat().st_mtime_ns,
                   (ROOT/'agent-tp/tests/firmware/lvgl_layer_memory.inc').stat().st_mtime_ns,
                   (ROOT/'task/DataCenter/SensorSnapshot.h').stat().st_mtime_ns,
                   (ROOT/'components/myLvgl/myLvgl.c').stat().st_mtime_ns,
                   max(p.stat().st_mtime_ns for p in (ROOT/'components/myLvgl/include').glob('*.h')),
                   max(p.stat().st_mtime_ns for p in (ROOT/'components/MyEncoder/include').glob('*.h')),
                   max(p.stat().st_mtime_ns for p in (ROOT/'agent-tp/tests/firmware/plantmilk_host').rglob('*.h')),
                   max(p.stat().st_mtime_ns for p in ui.rglob('*.h') if 'ui_update' not in p.parts))
        if not obj.exists() or obj.stat().st_mtime_ns < newest:
            result=subprocess.run([args.cc,*flags,'-c',str(source),'-o',str(obj)],capture_output=True,text=True)
            if result.returncode:raise RuntimeError(str(source)+'\n'+result.stdout+result.stderr)
        return obj
    print('Compiling real LVGL/UI sources:',len(sources),flush=True)
    with ThreadPoolExecutor(max_workers=6) as pool:objects=list(pool.map(compile_one,sources))
    response=OUT/'objects.rsp'
    response.write_text('\n'.join('"'+str(p).replace('\\','/')+'"' for p in objects),encoding='utf-8')
    exe=OUT/('test_plantmilk_ui.exe' if os.name=='nt' else 'test_plantmilk_ui')
    subprocess.run([args.cc,'@'+str(response),'-o',str(exe),'-static','-lm'],check=True)
    result=subprocess.run([str(exe),str(frames)],timeout=60,capture_output=True,text=True)
    (OUT/'run.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    print('\n'.join(result.stdout.splitlines()[-40:]))
    if result.returncode:raise SystemExit(result.stderr or 'Host UI test failed: '+str(result.returncode))
    if "bitmap not found" in result.stdout or "Couldn't create a new layer" in result.stdout:
        raise SystemExit('LVGL rendering failed; see '+str(OUT/'run.log'))
    print('Frames:',frames)

if __name__=='__main__':main()
