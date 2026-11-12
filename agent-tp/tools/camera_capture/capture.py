"""Capture the named DirectShow camera; never silently select another camera.

Dependencies: opencv-python-headless, pygrabber, comtypes.
Optional local dependencies live in build/host-deps; outputs default to agent-tp.
"""
import argparse
from datetime import datetime
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'build/host-deps'))
import cv2
from pygrabber.dshow_graph import FilterGraph


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--camera', default='SHL-500W')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    names = FilterGraph().get_input_devices()
    matches = [i for i, name in enumerate(names) if name == args.camera]
    if len(matches) != 1:
        raise RuntimeError(f'Expected exactly one {args.camera}: {names}')
    output = args.output or (ROOT / 'agent-tp/data/captures' /
                            datetime.now().strftime('%Y-%m-%d') /
                            datetime.now().strftime('%H%M%S-SHL-500W.png'))
    output.parent.mkdir(parents=True, exist_ok=True)
    capture = cv2.VideoCapture(matches[0], cv2.CAP_DSHOW)
    try:
        if not capture.isOpened():
            raise RuntimeError(f'Cannot open {args.camera}')
        capture.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
        capture.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)
        frame = None
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            ok, latest = capture.read()
            if ok:
                frame = latest
        if frame is None or not cv2.imwrite(str(output), frame):
            raise RuntimeError('No frame captured/saved')
        print(f'{args.camera}, DirectShow index {matches[0]}, '
              f'{frame.shape[1]}x{frame.shape[0]} -> {output}')
    finally:
        capture.release()


if __name__ == '__main__':
    main()
