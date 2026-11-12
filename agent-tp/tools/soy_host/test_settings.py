import tempfile
import unittest
from pathlib import Path

from settings import DEFAULT_SETTINGS, SettingsError, load_settings, save_settings


class TestSerialSettings(unittest.TestCase):
    def test_missing_file_returns_defaults(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(load_settings(Path(directory) / "missing.json"),
                             DEFAULT_SETTINGS)

    def test_round_trip(self):
        settings = {
            "command_port": "COM4",
            "temperature_port": "COM7",
            "command_baud": "115200",
            "temperature_baud": "9600",
            "same_port": False,
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config" / "settings.json"
            save_settings(settings, path)
            self.assertEqual(load_settings(path), settings)

    def test_invalid_file_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            path.write_text("{", encoding="utf-8")
            with self.assertRaises(SettingsError):
                load_settings(path)


if __name__ == "__main__":
    unittest.main()
