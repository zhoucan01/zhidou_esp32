#!/usr/bin/env python3
"""Clean up portable firmware bundles under build/, keeping only the newest ones."""
import argparse
import shutil
import sys
from pathlib import Path


def discover_bundles(build_dir):
    """Return firmware-* directories sorted by modification time (newest first)."""
    bundles = [p for p in build_dir.glob("firmware-*") if p.is_dir()]
    bundles.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return bundles


def main():
    root = Path(__file__).resolve().parents[2]
    build = root / "build"

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--keep", type=int, default=1,
        help="Number of newest bundles to keep (default: 1; use 0 to remove all)")
    parser.add_argument(
        "--dry-run", action="store_true",
        help="List what would be deleted without actually removing anything")
    parser.add_argument(
        "--build-dir", type=Path, default=build,
        help=f"Build directory (default: {build})")
    args = parser.parse_args()
    if args.keep < 0:
        parser.error("--keep must be >= 0")

    build_dir = args.build_dir.resolve()
    if not build_dir.is_dir():
        print(f"Build directory not found: {build_dir}", file=sys.stderr)
        sys.exit(1)

    bundles = discover_bundles(build_dir)
    if not bundles:
        print("No firmware bundles found.")
        return

    keep = args.keep
    to_keep = bundles[:keep]
    to_delete = bundles[keep:]

    print(f"Found {len(bundles)} firmware bundle(s) in {build_dir}")
    if to_keep:
        print("\nKeeping:")
        for p in to_keep:
            print(f"  + {p.name}")
    if not to_delete:
        print("\nNothing to delete.")
        return

    print("\nDeleting:" if not args.dry_run else "\nWould delete:")
    total_size = 0
    for p in to_delete:
        size = sum(f.stat().st_size for f in p.rglob("*") if f.is_file())
        total_size += size
        marker = "  - " if not args.dry_run else "  ~ "
        print(f"{marker}{p.name}  ({size / 1024:.1f} KB)")

    if args.dry_run:
        print(f"\nDry run: {len(to_delete)} bundle(s), "
              f"{total_size / 1024 / 1024:.2f} MB would be freed. No files removed.")
        return

    for p in to_delete:
        shutil.rmtree(p)
    print(f"\nDeleted {len(to_delete)} bundle(s), freed {total_size / 1024 / 1024:.2f} MB.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print(f"Clean failed: {error}", file=sys.stderr)
        sys.exit(1)
