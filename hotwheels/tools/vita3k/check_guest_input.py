#!/usr/bin/env python3
"""Require a DINPUT waypoint row with the exact requested key mask."""
import re
import sys


ROW = re.compile(r"^\[waypoint\]\s+\d+\s+-?\d+\s+-?\d+\s+(\d+)\s*$")


def main(argv):
    if len(argv) != 3 or not argv[1].startswith("waypoint-keymask:"):
        print("expected marker syntax: waypoint-keymask:1..15", file=sys.stderr)
        return 2
    try:
        expected = int(argv[1].split(":", 1)[1], 10)
    except ValueError:
        print("expected key mask must be decimal", file=sys.stderr)
        return 2
    if not 1 <= expected <= 15:
        print("expected key mask must be in 1..15", file=sys.stderr)
        return 2
    try:
        stream = sys.stdin if argv[2] == "-" else open(argv[2], encoding="utf-8", errors="replace")
        with stream:
            for line in stream:
                match = ROW.match(line.rstrip("\r\n"))
                if match and int(match.group(1), 10) == expected:
                    print(f"guest DINPUT waypoint keymask={expected}")
                    return 0
    except OSError as exc:
        print(f"cannot read guest log: {exc}", file=sys.stderr)
        return 2
    print(f"guest DINPUT waypoint keymask={expected} absent")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
