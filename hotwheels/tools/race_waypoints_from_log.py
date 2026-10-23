#!/usr/bin/env python3
"""Extrae muestras [waypoint] de run.stdout al formato de replay HW-030."""
import argparse
import re
import sys

ROW = re.compile(r"\[waypoint\]\s+(\d+)\s+(-?\d+)\s+(-?\d+)\s+(\d+)")


def extract(source):
    rows = []
    last_tick = -1
    for line in source:
        match = ROW.search(line)
        if not match:
            continue
        tick, x, z, keys = map(int, match.groups())
        world_limit = 1 << 27
        if (tick <= last_tick or tick > (1 << 32) - 1 or keys > 15 or
                not -world_limit <= x <= world_limit or not -world_limit <= z <= world_limit):
            raise ValueError("rango guest/keymask o ticks no monótonos")
        rows.append((tick, x, z, keys))
        last_tick = tick
    if len(rows) < 3:
        raise ValueError(f"se hallaron {len(rows)} muestras; se requieren al menos 3")
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", help="Vita3K run.stdout o log del runner")
    parser.add_argument("output", help="CSV whitespace para HW_RACE_WAYPOINTS")
    args = parser.parse_args()
    try:
        with open(args.log, encoding="utf-8", errors="replace") as source:
            rows = extract(source)
        with open(args.output, "w", encoding="ascii") as output:
            output.write("# tick x_q12 z_q12 keymask\n")
            for row in rows:
                output.write("%u %d %d %u\n" % row)
    except (OSError, ValueError) as error:
        print(f"race_waypoints_from_log: {error}", file=sys.stderr)
        return 1
    print(f"race_waypoints_from_log: {len(rows)} muestras -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
