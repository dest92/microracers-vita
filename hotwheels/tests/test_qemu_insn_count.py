#!/usr/bin/env python3
"""CA1 SPEC-053: parser del trace `qemu-arm -d nochain,exec,in_asm`.

Fixtures sintéticos con el formato real observado de qemu 8.2.2; no necesita qemu
para correr. El caso duro (ejecución sin `in_asm`) debe FALLAR, no subcontar.
"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = Path(os.environ.get("QEMU_INSN_COUNT", ROOT / "hotwheels/tools/qemu_insn_count.py"))

# Bloque A: 3 instrucciones (12 bytes) en dos líneas OBJD-T; se reimprime igual.
# Bloque B: 2 instrucciones (8 bytes).  A ejecutado 2 veces, B 1 vez.
BLOCK_A = "----------------\nIN: \n0x00010340:  \nOBJD-T: 4ff0000b4ff0000e\nOBJD-T: 02bc6a46\n"
EXEC_A = "Trace 0: 0x7a1538000100 [00800480/0000000000010340/00000000/00000200] \n"
BLOCK_B = ("----------------\nIN: foo\n0x0001055c:  \n"
           "OBJD-T: 2de9804817465a4b\n")
EXEC_B = "Trace 0: 0x7a1538000380 [00800480/000000000001055c/00000000/00000200] foo\n"


def run(text: str):
    with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False) as fh:
        fh.write(text)
        name = fh.name
    try:
        return subprocess.run([sys.executable, str(TOOL), name],
                              text=True, capture_output=True, check=False)
    finally:
        os.unlink(name)


def summary(text: str):
    res = run(text)
    if res.returncode != 0:
        return None, res
    line = next(l for l in res.stdout.splitlines() if l.startswith("qemu-insn:"))
    fields = dict(p.split("=", 1) for p in line.split("qemu-insn:", 1)[1].split())
    return {k: int(v) for k, v in fields.items()}, res


class Parser(unittest.TestCase):
    def test_counts_two_blocks_multiline_and_dedupe(self):
        # A reimpreso idéntico (no suma tb_unique) + ejecuciones repetidas.
        text = BLOCK_A + EXEC_A + EXEC_A + BLOCK_B + EXEC_B + BLOCK_A
        got, res = summary(text)
        self.assertIsNotNone(got, res.stderr)
        self.assertEqual(got["tb_unique"], 2)
        self.assertEqual(got["tb_exec"], 3)
        self.assertEqual(got["arm_insns"], 3 * 2 + 2 * 1)
        self.assertEqual(got["blocks_unknown"], 0)

    def test_address_reuse_counts_each_generation(self):
        # El dynarec reusa direcciones del cajón de código: la MISMA dirección puede
        # alojar bloques distintos (otro tamaño) más tarde. Cada ejecución se cuenta
        # con el tamaño vigente en ese momento, no con el último visto.
        reuse = (BLOCK_A + EXEC_A + EXEC_A +
                 "----------------\nIN: bar\n0x00010340:  \nOBJD-T: aabbccdd\n" + EXEC_A)
        got, res = summary(reuse)
        self.assertIsNotNone(got, res.stderr)
        self.assertEqual(got["tb_unique"], 1)
        self.assertEqual(got["tb_exec"], 3)
        self.assertEqual(got["arm_insns"], 3 * 2 + 1)

    def test_empty_log_is_zero_not_error(self):
        got, res = summary("")
        self.assertIsNotNone(got, res.stderr)
        self.assertEqual(got["arm_insns"], 0)
        self.assertEqual(got["tb_exec"], 0)

    def test_exec_without_in_asm_is_error(self):
        res = run(EXEC_B)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("sin in_asm", res.stderr)

    def test_malformed_trace_is_error(self):
        res = run(BLOCK_A + "Trace 0: no es una linea de trace\n")
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("Trace", res.stderr)

    def test_per_tb_table_ranks_by_instructions(self):
        with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False) as fh:
            fh.write(BLOCK_A + EXEC_A + EXEC_A + BLOCK_B + EXEC_B)
            name = fh.name
        try:
            res = subprocess.run([sys.executable, str(TOOL), "--per-tb", name],
                                 text=True, capture_output=True, check=False)
        finally:
            os.unlink(name)
        self.assertEqual(res.returncode, 0, res.stderr)
        rows = [l.split() for l in res.stdout.splitlines() if l.startswith("0x")]
        self.assertEqual(rows[0][0], "0x00010340")
        self.assertEqual(rows[0][1:], ["2", "6"])   # ejecuciones, instrucciones
        self.assertEqual(rows[1][0], "0x0001055c")
        self.assertEqual(rows[1][1:], ["1", "2"])


if __name__ == "__main__":
    unittest.main()
