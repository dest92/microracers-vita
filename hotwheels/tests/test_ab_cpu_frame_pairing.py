#!/usr/bin/env python3
"""Regresión de pares A/B ante una pata fallida; usa docker falso, sin QEMU.

Incluye el caso de locale del usuario (BUG-052): con `LC_NUMERIC=es_AR.UTF-8` el
formateo de la métrica no puede emitir coma decimal, o el agregado revienta.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
RUNNER = Path(os.environ.get("AB_CPU_FRAME_SCRIPT", ROOT / "hotwheels/tools/ab_cpu_frame.sh"))
FAKE_DOCKER = (
    "#!/usr/bin/env python3\n"
    "import os, pathlib, sys\n"
    "counter = pathlib.Path(os.environ['FAKE_DOCKER_COUNTER'])\n"
    "n = int(counter.read_text()) + 1 if counter.exists() else 1\n"
    "counter.write_text(str(n))\n"
    "mode = next((x for x in sys.argv if x.startswith('MODE=')), '')\n"
    "if n == 1:\n"
    "    sys.exit(1)  # pata A1 falla\n"
    "cpu = {'MODE=B': '2.0', 'MODE=A': '1.0'}[mode]\n"
    "if n == 4: cpu = '1.5'  # pata B2\n"
    "print('fin: shutdown requested frames=100 missing=0')\n"
    "print(f'cpu_user={cpu} cpu_sys=0.0')\n"
)


class PairingRegression(unittest.TestCase):
    def run_pairing(self, extra_env=None):
        """Corre 2 patas A/B con docker falso: A1 falla, B2 = 1.5."""
        with tempfile.TemporaryDirectory() as td:
            root = Path(td) / "repo"
            runner = root / "hotwheels/tools/ab_cpu_frame.sh"
            runner.parent.mkdir(parents=True)
            (root / "hotwheels/out").mkdir(parents=True)
            shutil.copy2(RUNNER, runner)

            bindir = Path(td) / "bin"
            bindir.mkdir()
            fake_docker = bindir / "docker"
            fake_docker.write_text(FAKE_DOCKER)
            fake_docker.chmod(0o755)
            env = os.environ.copy()
            env["PATH"] = f"{bindir}:{env['PATH']}"
            env["FAKE_DOCKER_COUNTER"] = str(Path(td) / "docker.calls")
            env.pop("LC_ALL", None)  # deja mandar a LC_NUMERIC (como el shell del usuario)
            env.update(extra_env or {})
            return subprocess.run(
                ["bash", str(runner), "1", "2", "MODE=A", "MODE=B"],
                env=env, text=True, capture_output=True, check=False,
            )

    def test_failed_a_discards_its_b_and_keeps_next_complete_pair(self):
        result = self.run_pairing({"LC_ALL": "C"})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("delta_medias=+50.0%", result.stdout)
        self.assertIn("delta_pareado_mediana=+50.0% pares=['+50.0']", result.stdout)
        self.assertIn("pata 1 B", result.stdout)
        self.assertIn("pata 2 A", result.stdout)
        self.assertIn("pata 2 B", result.stdout)

    @unittest.skipUnless(
        any(l.lower().startswith("es_ar") for l in subprocess.run(
            ["locale", "-a"], capture_output=True, text=True).stdout.splitlines()),
        "falta el locale es_AR en este host")
    def test_reporta_el_delta_con_lc_numeric_es_ar(self):
        result = self.run_pairing({"LC_NUMERIC": "es_AR.UTF-8"})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("delta_medias=+50.0%", result.stdout)
        self.assertIn("delta_pareado_mediana=+50.0%", result.stdout)


if __name__ == "__main__":
    unittest.main()
