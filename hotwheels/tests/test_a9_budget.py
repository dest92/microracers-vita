#!/usr/bin/env python3
"""HW-073 (perf-host) SPEC-054 CA1: modelo de presupuesto A9 del resampleo.

Convierte instrucciones por frame de salida (medidas con qemu-arm; ver
tools/a9/run_a9_budget.sh) en fracción de un core Cortex-A9. Puro: sin deps.
"""
import subprocess
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import a9_budget as m  # noqa: E402


class Budget(unittest.TestCase):
    def test_insn_and_cores(self):
        b = m.budget(789.1, 48000, 1)
        self.assertAlmostEqual(b["insn_s"], 789.1 * 48000.0, places=1)
        self.assertAlmostEqual(b["cores"], 789.1 * 48000.0 / m.A9_HZ, places=9)

    def test_zero_is_zero(self):
        for voices, insn in ((0, 789.1), (8, 0.0), (0, 0.0)):
            b = m.budget(insn, 48000, voices)
            self.assertEqual(b["insn_s"], 0.0)
            self.assertEqual(b["cores"], 0.0)
            self.assertEqual(b["cores_milli"], 0)

    def test_monotonic(self):
        a = m.budget(455.8, 48000, 4)["cores"]
        b = m.budget(455.8, 48000, 8)["cores"]
        c = m.budget(789.1, 48000, 8)["cores"]
        self.assertLess(a, b)
        self.assertLess(b, c)

    def test_cpi_scales(self):
        base = m.budget(301.0, 48000, 8, cpi=1.0)["cycles_s"]
        self.assertAlmostEqual(m.budget(301.0, 48000, 8, cpi=1.5)["cycles_s"], base * 1.5)

    def test_coeff_footprint(self):
        self.assertEqual(m.coeff_bytes(16), 32 * 256 * 16 * 2)  # 256 KiB totales
        self.assertEqual(m.coeff_bytes(16) // 32, 8192)          # 8 KiB por corte
        self.assertTrue(m.fits_l1d(16))
        self.assertFalse(m.fits_l1d(16, banks_active=8))


class Cli(unittest.TestCase):
    def test_emits_kv(self):
        out = subprocess.run(
            [sys.executable, str(TOOLS / "a9_budget.py"), "--insn-frame", "789.1",
             "--rate", "48000", "--voices", "8"],
            capture_output=True, text=True, check=True).stdout
        kv = dict(tok.split("=", 1) for tok in out.split() if "=" in tok)
        self.assertEqual(kv["voices"], "8")
        self.assertEqual(kv["core"], "a9")
        # 789.1*48000*8/444e6 = 0.6828 cores -> 682-683 milésimas
        self.assertIn(int(kv["cores_milli"]), (682, 683))


if __name__ == "__main__":
    unittest.main(verbosity=2)
