#!/usr/bin/env python3
"""BUG-049: la suite del replay de anillos debe ser ejecutable sin runner.

`tests/run_all.sh` (etapa 3) corre todas las suites `tests/test_*.py` como
`python3 <archivo>`, sin argumentos: es un glob, no un registro con su comando.
La suite del replay además es un driver de ctest, así que su bloque de entrada
resuelve el binario `image_ring_replay` desde un insumo externo.

Sin ese insumo la suite tiene que **saltarse** (motivo explícito), no morir
durante el import con IndexError: un rojo de infraestructura no puede
confundirse con un rojo de la imagen en el gate de humo.
"""
import importlib.util
import os
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUITE = ROOT / "tests/test_image_ring_replay.py"
BINARY_ENV = "HW_IMAGE_RING_REPLAY"


def env_without_binary():
    return {k: v for k, v in os.environ.items() if k != BINARY_ENV}


class ImageRingReplayStandaloneTests(unittest.TestCase):
    def test_suite_without_binary_skips_instead_of_crashing(self):
        result = subprocess.run(
            [sys.executable, str(SUITE)],
            env=env_without_binary(),
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertNotIn("IndexError", result.stderr)
        self.assertIn("skipped=", result.stderr)
        self.assertIn("RUN_MODE: skip", result.stdout)

    def test_suite_imports_without_binary_and_marks_its_tests_skipped(self):
        env = env_without_binary()
        env["PYTHONPATH"] = str(ROOT)
        code = (
            "import importlib.util, sys, unittest\n"
            "spec = importlib.util.spec_from_file_location('replay_suite', sys.argv[1])\n"
            "mod = importlib.util.module_from_spec(spec)\n"
            "spec.loader.exec_module(mod)\n"
            "def flatten(s):\n"
            "    for item in s:\n"
            "        if isinstance(item, unittest.TestSuite):\n"
            "            yield from flatten(item)\n"
            "        else:\n"
            "            yield item\n"
            "suite = unittest.TestLoader().loadTestsFromModule(mod)\n"
            "cases = list(flatten(suite))\n"
            "print('cases', len(cases))\n"
            "for case in cases:\n"
            "    print('skip', getattr(case, '__unittest_skip__', False),\n"
            "          getattr(case, '__unittest_skip_why__', ''))\n"
        )
        result = subprocess.run(
            [sys.executable, "-c", code, str(SUITE)],
            env=env,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("cases 8", result.stdout)
        skipped = [line for line in result.stdout.splitlines() if line.startswith("skip ")]
        self.assertEqual(len(skipped), 8, result.stdout)
        for line in skipped:
            self.assertRegex(line, r"^skip True .+", result.stdout)


if __name__ == "__main__":
    unittest.main()
