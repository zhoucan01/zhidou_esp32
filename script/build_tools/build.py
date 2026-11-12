#!/usr/bin/env python3
"""Build with an activated ESP-IDF environment and export a portable flash bundle."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def app_offset(project_root):
    """Read the app partition offset from partitions.csv.

    ESP-IDF 5.3 leaves the app entry offset empty in flasher_args.json when the
    project uses a custom OTA layout, so the real offset has to come from the
    partition table itself.
    """
    for line in (project_root / "partitions.csv").read_text(encoding="utf-8").splitlines():
        fields = [field.strip() for field in line.split(",")]
        if len(fields) >= 4 and fields[1] == "app" and fields[3]:
            return int(fields[3], 0)
    raise RuntimeError("Cannot determine the app partition offset from partitions.csv")


def merge_flash_files(manifest, source_root, destination, project_root):
    images = []
    end = 0
    for offset, filename in manifest["flash_files"].items():
        address = int(offset, 0) if offset.strip() else app_offset(project_root)
        data = (source_root / filename).read_bytes()
        images.append((address, data))
        end = max(end, address + len(data))
    merged = bytearray(b"\xff" * end)
    for address, data in images:
        merged[address:address + len(data)] = data
    destination.write_bytes(merged)


def main():
    root = Path(__file__).resolve().parents[2]
    build = root / "build"
    idf = os.environ.get("IDF_PATH")
    if not idf or not (Path(idf) / "tools/idf.py").is_file():
        raise RuntimeError("Activate ESP-IDF first (run its export.ps1 / export.sh).")
    env = os.environ.copy()
    env["CCACHE_DIR"] = str(build / "ccache")
    env["CCACHE_TEMPDIR"] = str(build / "ccache/tmp")
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    # 关闭 IDF 组件管理器：第三方组件已本地化到 components/ 下，
    # 避免 idf.py 在启动时加载组件管理器扩展并访问 esp-registry（网络抖动会导致构建卡住）。
    env["IDF_COMPONENT_MANAGER"] = "0"
    (build / "ccache/tmp").mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [sys.executable, str(Path(idf) / "tools/idf.py"), "-B", str(build), "build"],
        cwd=root, env=env, check=True,
    )
    manifest = json.loads((build / "flasher_args.json").read_text(encoding="utf-8"))
    # Unique bundles never overwrite a previously exported release.
    import tempfile
    bundle = Path(tempfile.mkdtemp(prefix="firmware-", dir=build))
    merged_name = "firmware.bin"
    merge_flash_files(manifest, build, bundle / merged_name, root)
    manifest["flash_files"] = {"0x0": merged_name}
    # Keep only the fields needed by the standalone downloader.
    portable = {key: manifest[key] for key in
                ("flash_files", "write_flash_args", "extra_esptool_args")}
    (bundle / "flasher_args.json").write_text(
        json.dumps(portable, indent=2) + "\n", encoding="utf-8")
    shutil.copy2(root / "script/flash/flash.py", bundle / "flash.py")
    shutil.copy2(root / "flash.bat", bundle / "flash.bat")
    shutil.copy2(root / "script/README.md", bundle / "README.md")
    print(f"\nBuild succeeded. Portable firmware bundle: {bundle}")
    print(f'Flash: "{sys.executable}" "{bundle / "flash.py"}" --port COM4')


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Build failed: {error}", file=sys.stderr)
        sys.exit(1)
