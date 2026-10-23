#!/usr/bin/env python3
"""Contrato del canal XTest opcional del runner headless Vita3K."""
from pathlib import Path
import os
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
RUNNER = (ROOT / "tools/vita3k_run.sh").read_text()
IMAGE = (ROOT / "tools/Dockerfile.vita3k-headless").read_text()
INJECTOR = ROOT / "tools/vita3k/send_keys.c"
INJECTOR_SOURCE = INJECTOR.read_text()


class Vita3KInputWiring(unittest.TestCase):
    def test_runner_disables_startup_update_prompt(self):
        self.assertIn('check-for-updates-mode: 0', RUNNER,
                      "el harness debe desactivar el diálogo de update en config.yml")

    def test_render_scale_defaults_to_native(self):
        config_h = (ROOT / "src/io/config.h").read_text()
        self.assertIn("RenderScale { Original, Native } render_scale = RenderScale::Native", config_h,
                      "hwvita.ini ausente o sin render_scale debe rasterizar a resolución nativa")

    def test_events_are_forwarded_to_container(self):
        self.assertTrue('"HW_VITA3K_KEY_EVENTS=${HW_VITA3K_KEY_EVENTS:-}"' in RUNNER,
                        "runner debe pasar la variable opcional al contenedor")
        self.assertTrue("HW_VITA3K_KEY_EVENTS=" in RUNNER and "docker run" in RUNNER,
                        "variable no conectada a docker run")
        self.assertTrue("HW_VITA3K_INPUT_EXPECT" in RUNNER,
                        "la inyección debe exigir respuesta guest observable")

    def test_injection_without_guest_marker_is_rejected_before_launch(self):
        env = {k: v for k, v in os.environ.items()
               if k not in ("HW_VITA3K_KEY_EVENTS", "HW_VITA3K_INPUT_EXPECT")}
        env["HW_VITA3K_KEY_EVENTS"] = "1:Up:0.1"
        result = subprocess.run(
            ["bash", str(ROOT / "tools/vita3k_run.sh"), "missing.vpk", "1"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=5,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("HW_VITA3K_INPUT_EXPECT", result.stderr)

    def test_vita3k_update_config_assert_uses_real_whitespace_regex(self):
        self.assertIn(r'assert re.search(r"(?m)^check-for-updates-mode:\s*0\s*$", text)', RUNNER,
                      "la verificación del config debe aceptar espacios reales, no backslash literal")

    def test_guest_gate_requires_waypoint_keymask_not_any_waypoint(self):
        self.assertIn("check_guest_input.py", RUNNER,
                      "runner debe validar una respuesta DINPUT con máscara de teclas")
        self.assertIn("waypoint-keymask:", RUNNER,
                      "el marcador debe identificar la máscara observada")
        checker = ROOT / "tools/vita3k/check_guest_input.py"
        self.assertTrue(checker.is_file())

        def check(log, expected="waypoint-keymask:1"):
            return subprocess.run(
                ["python3", str(checker), expected, "-"], input=log,
                capture_output=True, text=True, timeout=5,
            )

        self.assertNotEqual(check("[waypoint] 8 123 -4 0\n").returncode, 0)
        self.assertNotEqual(check("[waypoint] 8 123 -4 2\n").returncode, 0)
        self.assertEqual(check("[waypoint] 8 123 -4 1\n").returncode, 0)
        self.assertEqual(check("[waypoint] 8 123 -4 0\n[waypoint] 9 123 -4 1\n").returncode, 0)

    def test_inner_runner_logs_optional_injector(self):
        self.assertTrue('if [ -n "${HW_VITA3K_KEY_EVENTS:-}" ]' in RUNNER,
                        "sin eventos configurados el inyector debe quedar deshabilitado")
        self.assertTrue('"$HW_VITA3K_KEY_EVENTS"' in RUNNER,
                        "inyector no recibe la secuencia")
        self.assertTrue("send_keys.log" in RUNNER and "wait \"$KP\"" in RUNNER,
                        "runner debe guardar y esperar el resultado del inyector")

    def test_headless_image_builds_xtest_injector(self):
        self.assertTrue(INJECTOR.is_file())
        self.assertTrue("libxtst-dev" in IMAGE and "gcc -O2" in IMAGE and "send_keys.c" in IMAGE,
                        "la imagen debe compilar el inyector con XTest")

    def test_viewport_click_keeps_child_focus(self):
        self.assertEqual(INJECTOR_SOURCE.count("focus_app_window(d);"), 1,
                         "no robar foco al widget después del click al viewport")

    @unittest.skipUnless(shutil.which("docker"), "Docker no instalado")
    def test_xtest_events_reach_an_xvfb_client(self):
        image = "vita3k-headless:26.10-xtest2"
        if subprocess.run(["docker", "image", "inspect", image],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            self.skipTest("imagen Vita3K headless no construida")
        script = (
            "set -e; Xvfb :99 -screen 0 1280x720x24 >/tmp/xvfb.log 2>&1 & xv=$!; "
            "trap 'kill $xv 2>/dev/null || true' EXIT; sleep 1; "
            "DISPLAY=:99 xev -geometry 1000x680+0+0 >/tmp/xev.log 2>&1 & ev=$!; "
            "sleep 1; DISPLAY=:99 /usr/local/bin/hw-send-keys '1:Up:0.1'; sleep 1; "
            "kill $ev 2>/dev/null || true; wait $ev 2>/dev/null || true; "
            "grep -q 'KeyPress event' /tmp/xev.log; grep -q 'KeyRelease event' /tmp/xev.log"
        )
        result = subprocess.run(
            ["docker", "run", "--rm", "--entrypoint", "bash", image, "-lc", script],
            capture_output=True, text=True, timeout=15,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
