"""El veredicto del smoke tiene que registrar la carga del host en el motivo.

`BUG-054`: con loadavg alto el watchdog de shutdown falla de forma intermitente
(`salida forzada`) con el guest sano. Para poder distinguir ruido de carga de una
regresión, el motivo (`base=… frames=… fin=… forzada=…`) debe incluir la carga del
host medida al emitir el veredicto: `load=1.73`.

Casos: corrida limpia (OK) y corrida forzada (FAIL), ambos con `load=` numérico.
"""
import pathlib
import re
import subprocess
import tempfile
import unittest

HW = pathlib.Path(__file__).resolve().parents[1]
VERDICT = HW / "tools" / "smoke_qemu_verdict.sh"
LOAD = re.compile(r"\bload=\d+(?:\.\d+)?\b")


def make_run(tmp: pathlib.Path, *, frames: int, fin: bool, forced: bool) -> pathlib.Path:
    d = tmp / ("run-" + ("ok" if fin and not forced else "bad"))
    d.mkdir()
    lines = ["[hw] base=0x00400000 entry=0x00452f05 imports=94"]
    if fin:
        lines.append("[hw] fin: shutdown requested exit=0x00000000 frames=100 missing=0")
    if forced:
        lines.append("[hw] el scheduler no paró en 3 s: salida forzada")
    (d / "run.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
    for i in range(frames):
        (d / f"frame_{i}.png").write_bytes(b"")
    return d


class MotivoConCarga(unittest.TestCase):
    def parse(self, d):
        r = subprocess.run(["bash", str(VERDICT), str(d)], capture_output=True, text=True)
        lines = r.stdout.splitlines()
        self.assertGreaterEqual(len(lines), 2, r.stdout + r.stderr)
        return r.returncode, lines[0], lines[1]

    def test_ok_incluye_load(self):
        with tempfile.TemporaryDirectory() as t:
            rc, ver, motivo = self.parse(make_run(pathlib.Path(t), frames=3, fin=True, forced=False))
        self.assertEqual((rc, ver), (0, "OK"), motivo)
        self.assertRegex(motivo, LOAD, f"el motivo OK no trae la carga del host: {motivo!r}")

    def test_fail_incluye_load(self):
        with tempfile.TemporaryDirectory() as t:
            rc, ver, motivo = self.parse(make_run(pathlib.Path(t), frames=2, fin=False, forced=True))
        self.assertEqual((rc, ver), (1, "FAIL"), motivo)
        self.assertRegex(motivo, LOAD, f"el motivo FAIL no trae la carga del host: {motivo!r}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
