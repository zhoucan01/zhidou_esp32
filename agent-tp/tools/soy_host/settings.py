"""Persistent serial-port preferences for the soy host application."""
import json
import os
from pathlib import Path


DEFAULT_SETTINGS = {
    "command_port": "",
    "temperature_port": "",
    "command_baud": "115200",
    "temperature_baud": "115200",
    "same_port": False,
}


class SettingsError(Exception):
    pass


def settings_path():
    if os.name == "nt":
        base = os.environ.get("APPDATA")
        if base:
            return Path(base) / "SoyHost" / "settings.json"
    return Path.home() / ".config" / "soy_host" / "settings.json"


def load_settings(path=None):
    target = Path(path) if path is not None else settings_path()
    try:
        with target.open("r", encoding="utf-8") as stream:
            saved = json.load(stream)
    except FileNotFoundError:
        return DEFAULT_SETTINGS.copy()
    except (OSError, json.JSONDecodeError) as error:
        raise SettingsError(f"无法读取串口设置：{error}") from error

    if not isinstance(saved, dict):
        raise SettingsError("串口设置文件格式无效")
    settings = DEFAULT_SETTINGS.copy()
    for key in ("command_port", "temperature_port",
                "command_baud", "temperature_baud"):
        value = saved.get(key, settings[key])
        if not isinstance(value, str):
            raise SettingsError(f"串口设置字段 {key} 格式无效")
        settings[key] = value
    same_port = saved.get("same_port", settings["same_port"])
    if not isinstance(same_port, bool):
        raise SettingsError("串口设置字段 same_port 格式无效")
    settings["same_port"] = same_port
    return settings


def save_settings(settings, path=None):
    target = Path(path) if path is not None else settings_path()
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("w", encoding="utf-8") as stream:
            json.dump(settings, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
    except OSError as error:
        raise SettingsError(f"无法保存串口设置：{error}") from error
