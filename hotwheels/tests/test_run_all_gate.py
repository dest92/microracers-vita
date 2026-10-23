"""Gate de `tests/run_all.sh`: descubre `tests/test_*.sh` y les pone timeout por suite.

Motivo (BUG-053/BUG-003): `run_all` corría `ctest`, `tests/run_*.sh` y `tests/test_*.py`,
pero nunca las suites `tests/test_*.sh` — entre ellas `test_smoke.sh`, la regresión del
veredicto qemu que habría cazado el falso negativo en HEAD.

Contrato que fija este test:
  1. `run_all.sh --list` lista cada `tests/test_*.sh` exactamente una vez.
  2. La etapa nueva corre cada suite con `timeout RUN_ALL_TEST_SH_TIMEOUT` (default 120 s):
     una suite que cuelga termina con rc=124 y cuenta como FAIL.
  3. Las suites que necesitan insumos NO versionados (exe del juego, toolchains, consola)
     están en `tests/test_sh_env_skip.txt` y se reportan SKIP, no FAIL.
"""
import os
import pathlib
import subprocess
import tempfile
import unittest

HW = pathlib.Path(__file__).resolve().parents[1]
RUN_ALL = HW / "tests" / "run_all.sh"
TESTS = HW / "tests"


def run_all(*args, env=None):
    """Corre run_all.sh sin heredar knobs de la ronda (XFAIL/integración)."""
    e = os.environ.copy()
    for k in ("RUN_ALL_XFAIL", "RUN_ALL_INTEGRATION", "RUN_ALL_TESTS_DIR", "RUN_ALL_TEST_SH_TIMEOUT"):
        e.pop(k, None)
    if env:
        e.update(env)
    return subprocess.run(
        ["bash", str(RUN_ALL), *args],
        cwd=str(HW), env=e, capture_output=True, text=True, timeout=600,
    )


class Listado(unittest.TestCase):
    def test_list_incluye_las_suites_test_sh(self):
        suites = sorted(p.name for p in TESTS.glob("test_*.sh"))
        self.assertGreater(len(suites), 0, "no hay tests/test_*.sh en el árbol")
        out = run_all("--list")
        self.assertEqual(out.returncode, 0, out.stderr)
        listed = [line.strip() for line in out.stdout.splitlines() if line.strip()]
        for name in suites:
            self.assertEqual(
                listed.count(name), 1,
                f"{name} debe figurar una sola vez en --list; salida={listed}",
            )
        self.assertNotIn("run_all.sh", listed, "run_all no debe listarse a sí mismo")


class EtapaTestSh(unittest.TestCase):
    """La etapa nueva: timeout por suite y SKIP explícito de las de entorno."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = pathlib.Path(self.tmp.name)
        (d / "test_ok.sh").write_text("#!/usr/bin/env bash\necho ok\n", encoding="utf-8")
        (d / "test_slow.sh").write_text("#!/usr/bin/env bash\nsleep 30\n", encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def test_timeout_por_suite(self):
        r = run_all(
            "--only-test-sh",
            env={"RUN_ALL_TESTS_DIR": self.tmp.name, "RUN_ALL_TEST_SH_TIMEOUT": "1"},
        )
        self.assertNotEqual(r.returncode, 0, "una suite colgada debe hacer fallar el gate")
        self.assertIn("PASS test_ok.sh", r.stdout)
        self.assertIn("FAIL test_slow.sh (rc=124)", r.stdout)
        self.assertIn("run_all: PASS=1 FAIL=1", r.stdout)

    def test_rc77_es_skip(self):
        """Convenio automake: la suite que no puede correr sale 77 → SKIP, no FAIL.

        P3 del review de rev-gfx (2026-10-21): `test_vpk.sh` / `test_console_package.sh`
        usan `skip(){ ...; exit 77; }` cuando falta el SDK; sin SDK el gate se ponía
        rojo por entorno en vez de reportar SKIP.
        """
        d = pathlib.Path(self.tmp.name)
        (d / "test_slow.sh").unlink(missing_ok=True)
        (d / "test_skip77.sh").write_text(
            "#!/usr/bin/env bash\necho 'SKIP: falta toolchain'\nexit 77\n", encoding="utf-8"
        )
        r = run_all("--only-test-sh", env={"RUN_ALL_TESTS_DIR": self.tmp.name})
        self.assertEqual(r.returncode, 0, f"rc=77 significa SKIP, no FAIL:\n{r.stdout}")
        self.assertIn("SKIP test_skip77.sh", r.stdout)
        self.assertIn("PASS test_ok.sh", r.stdout)
        self.assertIn("run_all: PASS=1 FAIL=0", r.stdout)

    def test_skip_de_entorno_documentado(self):
        lista = TESTS / "test_sh_env_skip.txt"
        self.assertTrue(
            lista.exists(),
            "falta tests/test_sh_env_skip.txt: las suites que necesitan insumos no "
            "versionados deben declararse ahí (y reportarse SKIP), no fallar en HEAD",
        )
        nombres = [
            line.split("#")[0].strip()
            for line in lista.read_text(encoding="utf-8").splitlines()
        ]
        nombres = [n for n in nombres if n]
        self.assertTrue(nombres, "test_sh_env_skip.txt no declara ninguna suite")
        for n in nombres:
            self.assertTrue((TESTS / n).exists(), f"{n} está en la lista pero no existe")
        # Mecanismo: una suite cuyo nombre está en la lista se saltea aunque falle,
        # así el gate en HEAD no depende de insumos no versionados (exe, toolchains).
        d = pathlib.Path(self.tmp.name)
        (d / "test_slow.sh").unlink(missing_ok=True)  # acá sólo interesa el SKIP
        (d / nombres[0]).write_text("#!/usr/bin/env bash\nexit 1\n", encoding="utf-8")
        r = run_all("--only-test-sh", env={"RUN_ALL_TESTS_DIR": self.tmp.name})
        self.assertEqual(r.returncode, 0, f"la suite declarada debe saltearse:\n{r.stdout}")
        self.assertIn(f"SKIP {nombres[0]}", r.stdout)
        self.assertIn("PASS test_ok.sh", r.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
