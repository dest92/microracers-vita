#!/usr/bin/env python3
"""Promote the presenter's rolling tail to ordinary frame_NNNNN.png names."""
import re
import sys
from pathlib import Path

RECENT = re.compile(r"^frame_recent_[0-9]{2}_([0-9]+)\.png$")


def finalize(directory: Path) -> None:
    for source in directory.glob("frame_recent_*.png"):
        match = RECENT.match(source.name)
        if not match:
            continue
        target = directory / f"frame_{int(match.group(1)):05d}.png"
        if target.exists():
            source.unlink()
        else:
            source.rename(target)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: finalize_frames.py RUN_DIR")
    finalize(Path(sys.argv[1]))
