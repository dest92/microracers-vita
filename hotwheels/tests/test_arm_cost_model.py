#!/usr/bin/env python3
"""CA3 SPEC-053: modelo de costo por función (instrucciones ARM/frame + ciclos A9).

Entradas sintéticas: el conteo global de qemu (`qemu-insn:`) y la distribución por
función de HW-051 (`pcprof-game`). Verifica el reparto, la normalización por frame,
la estimación de ciclos y los bordes (0 frames, faltantes) sin correr qemu.
"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = Path(os.environ.get("ARM_COST_MODEL", ROOT / "hotwheels/tools/arm_cost_model.py"))

COUNTS = "qemu-insn: tb_unique=120 tb_exec=900000 arm_insns=1000000 blocks_unknown=0\n"
PROFILE = (
    "pcprof: scene=race total=10000 translated=8000 native=2000\n"
    "pcprof-game scene=race rank=1 eip=0x004521c4 name=__ftol samples=606 pct=6.06\n"
    "pcprof-game scene=race rank=2 eip=0x00450af3 name=FUN_00450af3 samples=300 pct=3.00\n"
)


def run(counts: str, profile: str, frames: str, *extra: str):
    with tempfile.TemporaryDirectory() as td:
        c = Path(td) / "counts.txt"
        p = Path(td) / "profile.txt"
        c.write_text(counts)
        p.write_text(profile)
        return subprocess.run([sys.executable, str(TOOL), "--counts", str(c),
                               "--profile", str(p), "--scene", "race", "--frames", frames, *extra],
                              text=True, capture_output=True, check=False)


def parse(res):
    summary = {}
    rows = []
    for line in res.stdout.splitlines():
        if line.startswith("cost-model:"):
            summary = {k: v for k, v in
                       (f.split("=", 1) for f in line.split("cost-model:", 1)[1].split())}
        elif line.startswith("  0x"):
            rows.append(line.split())
    return summary, rows


class CostModel(unittest.TestCase):
    def test_allocates_and_estimates_cycles(self):
        res = run(COUNTS, PROFILE, "100", "--cpi", "2.0", "--clock-mhz", "444")
        self.assertEqual(res.returncode, 0, res.stderr)
        s, rows = parse(res)
        self.assertEqual(s["arm_insns"], "1000000")
        self.assertAlmostEqual(float(s["arm_insns_per_frame"]), 10000.0)
        self.assertAlmostEqual(float(s["est_cycles_per_frame"]), 20000.0)
        self.assertAlmostEqual(float(s["est_fps_ceiling"]), 444e6 / 20000.0, places=1)
        self.assertAlmostEqual(float(s["profile_pct_sum"]), 9.06, places=2)
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0][1], "__ftol")
        self.assertAlmostEqual(float(rows[0][2].split("=")[1]), 606.0)   # instr/frame
        self.assertAlmostEqual(float(rows[0][3].split("=")[1]), 1212.0)  # ciclos/frame

    def test_zero_frames_is_error(self):
        res = run(COUNTS, PROFILE, "0")
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("frames", res.stderr)

    def test_missing_counts_is_error(self):
        res = run("sin resumen\n", PROFILE, "10")
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("qemu-insn", res.stderr)

    def test_missing_scene_is_error(self):
        res = run(COUNTS, PROFILE, "10", "--scene", "menu")
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("menu", res.stderr)

    def test_top_limits_rows(self):
        res = run(COUNTS, PROFILE, "100", "--top", "1")
        self.assertEqual(res.returncode, 0, res.stderr)
        _, rows = parse(res)
        self.assertEqual(len(rows), 1)


if __name__ == "__main__":
    unittest.main()
