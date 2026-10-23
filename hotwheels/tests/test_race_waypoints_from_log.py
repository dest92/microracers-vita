#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

PATH = Path(__file__).resolve().parents[1] / "tools/race_waypoints_from_log.py"
SPEC = importlib.util.spec_from_file_location("race_waypoints_from_log", PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ExtractWaypointsTest(unittest.TestCase):
    def test_ignores_non_waypoint_lines_and_parses_guest_log(self):
        rows = MODULE.extract([
            "boot ok\n",
            "[vita] [waypoint] 0 -10 20 1\n",
            "[waypoint] 8 -12 30 5\n",
            "[waypoint] 16 -15 40 9\n",
        ])
        self.assertEqual(rows, [(0, -10, 20, 1), (8, -12, 30, 5), (16, -15, 40, 9)])

    def test_rejects_short_or_bad_route(self):
        with self.assertRaises(ValueError):
            MODULE.extract(["[waypoint] 0 0 0 1\n", "[waypoint] 8 0 1 1\n"])
        with self.assertRaises(ValueError):
            MODULE.extract([
                "[waypoint] 0 0 0 1\n",
                "[waypoint] 8 0 1 1\n",
                "[waypoint] 8 0 2 1\n",
            ])
        with self.assertRaises(ValueError):
            MODULE.extract([
                "[waypoint] 0 0 0 1\n",
                "[waypoint] 8 0 1 1\n",
                "[waypoint] 16 268435456 2 1\n",
            ])
        with self.assertRaises(ValueError):
            MODULE.extract([
                "[waypoint] 0 0 0 1\n",
                "[waypoint] 8 0 1 1\n",
                "[waypoint] 4294967296 0 2 1\n",
            ])


if __name__ == "__main__":
    unittest.main()
