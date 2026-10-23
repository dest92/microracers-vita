#!/usr/bin/env python3
"""Snapshot a persistent log and capture the bytes written by one run."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def prefix_digest(source: Path, size: int) -> str:
    h = hashlib.sha256()
    with source.open("rb") as stream:
        remaining = size
        while remaining:
            block = stream.read(min(1024 * 1024, remaining))
            if not block:
                break
            h.update(block)
            remaining -= len(block)
    return h.hexdigest()


def snapshot(source: Path, state_path: Path) -> None:
    try:
        st = source.stat()
        state = {
            "exists": True,
            "size": st.st_size,
            "device": st.st_dev,
            "inode": st.st_ino,
            "prefix_sha256": prefix_digest(source, st.st_size),
        }
    except FileNotFoundError:
        state = {"exists": False, "size": 0, "device": None, "inode": None, "prefix_sha256": None}
    state_path.parent.mkdir(parents=True, exist_ok=True)
    state_path.write_text(json.dumps(state, sort_keys=True) + "\n")


def extract(source: Path, state_path: Path, output: Path) -> None:
    state = json.loads(state_path.read_text())
    try:
        st = source.stat()
        same_file = (
            state["exists"]
            and st.st_dev == state["device"]
            and st.st_ino == state["inode"]
            and st.st_size >= state["size"]
            and prefix_digest(source, state["size"]) == state["prefix_sha256"]
        )
        start = state["size"] if same_file else 0
        with source.open("rb") as stream:
            stream.seek(start)
            data = stream.read()
    except FileNotFoundError:
        data = b""

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    snap = commands.add_parser("snapshot")
    snap.add_argument("source", type=Path)
    snap.add_argument("state", type=Path)
    delta = commands.add_parser("extract")
    delta.add_argument("source", type=Path)
    delta.add_argument("state", type=Path)
    delta.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.command == "snapshot":
        snapshot(args.source, args.state)
    else:
        extract(args.source, args.state, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
