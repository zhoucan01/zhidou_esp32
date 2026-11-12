"""Install/check the repo Codex entry; shared content remains in agent-tp.

Run from any directory. This only copies the existing Copilot discovery entry
to .agents/skills/esp32s3-firmware/SKILL.md, never changes user-level settings.
"""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="check without writing")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    source = root / ".github/skills/esp32s3-firmware/SKILL.md"
    target = root / ".agents/skills/esp32s3-firmware/SKILL.md"
    shared = root / "agent-tp/skills/esp32s3-firmware/SKILL.md"
    if not shared.is_file():
        parser.error(f"Shared skill is missing: {shared}")
    content = source.read_bytes()
    if args.check:
        if not target.is_file() or target.read_bytes() != content:
            parser.exit(1, "Codex entry is missing or differs from the Copilot entry.\n")
        print("PASS: Codex/Copilot discovery entries match; shared skill exists.")
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(content)
    print(f"Installed Codex project skill entry: {target}")


if __name__ == "__main__":
    main()
