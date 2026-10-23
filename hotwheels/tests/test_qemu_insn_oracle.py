#!/usr/bin/env python3
"""CA2 SPEC-053: oráculo real del conteo de instrucciones ARM.

Dos programas ARM estáticos (`_start`, sin libc) que sólo difieren en 50 `add`
rectos: la diferencia de instrucciones ejecutadas DEBE ser exactamente 50. Así se
valida que el parser cuenta instrucciones ARM reales (bytes/4) y no líneas del log.
Corre qemu-arm dentro de la imagen hw-host-arm; si no hay docker, SKIP.
"""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "hotwheels/tools/qemu_insn_count.py"
IMAGE = "hw-host-arm:24.04"

HEAD = ".text\n.global _start\n_start:\n"
TAIL = "    mov r0, #0\n    mov r7, #1\n    svc #0\n"
EXTRA = 50


def program(extra: int) -> str:
    return HEAD + "".join("    add r0, r0, #1\n" for _ in range(extra)) + TAIL


def have_docker() -> bool:
    if not shutil.which("docker"):
        return False
    return subprocess.run(["docker", "image", "inspect", IMAGE],
                          capture_output=True).returncode == 0


@unittest.skipUnless(have_docker(), f"requiere docker + {IMAGE}")
class Oracle(unittest.TestCase):
    def test_instruction_delta_is_exact(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            (work / "a.s").write_text(program(0))
            (work / "b.s").write_text(program(EXTRA))
            script = (
                "set -e; cd /t;"
                "for p in a b; do arm-linux-gnueabihf-gcc -nostdlib -static -marm "
                "-march=armv7-a+simd -mfpu=neon -mfloat-abi=hard -o $p $p.s;"
                "qemu-arm -d nochain,exec,in_asm ./$p >/dev/null 2>$p.trace; done"
            )
            res = subprocess.run(["docker", "run", "--rm", "--user", f"{os.getuid()}:{os.getgid()}",
                                  "-v", f"{work}:/t", "-w", "/t", IMAGE, "bash", "-c", script],
                                 text=True, capture_output=True, check=False)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)

            counts = {}
            for p in ("a", "b"):
                out = subprocess.run(["python3", str(TOOL), str(work / f"{p}.trace")],
                                     text=True, capture_output=True, check=True)
                counts[p] = int(next(l for l in out.stdout.splitlines()
                                     if l.startswith("qemu-insn:")).split("arm_insns=")[1].split()[0])
            self.assertEqual(counts["b"] - counts["a"], EXTRA,
                             f"conteo no exacto: {counts}")


if __name__ == "__main__":
    unittest.main()
