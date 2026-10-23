#!/usr/bin/env python3
"""Contrato estático de la traza de calidad, que puede activarse en consola."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ImageTraceTest(unittest.TestCase):
    def test_trace_is_opt_in_and_reports_chain(self):
        runtime = (ROOT / "src/runtime_vita.cpp").read_text()
        integration = (ROOT / "src/host/integrate.cpp").read_text()
        d3d_bridge = (ROOT / "src/d3d/bridge.cpp").read_text()
        self.assertIn('getenv("HW_IMAGE_TRACE")', runtime)
        self.assertIn("settings candidate=", runtime)
        self.assertIn("prefix_hex=", runtime)
        self.assertIn("filter=", runtime)
        self.assertIn("viewport=", runtime)
        self.assertIn("cfg.image_trace = image_trace", runtime)
        self.assertIn("image_trace", integration)
        self.assertIn("SetDisplayMode request=", integration)
        self.assertIn("image_log", d3d_bridge)
        generic_shims = (ROOT.parent / "winx86-dx/src/shims/imports.cpp").read_text()
        self.assertIn('services.trace_settings', generic_shims)
        self.assertIn('settings read guest=', generic_shims)
        self.assertIn('image_capture_path += "image_capture.hwr"', runtime)
        self.assertIn("pthread_create(&image_capture_writer", runtime)
        self.assertIn("pthread_create writer rc=%d; captura desactivada", runtime)
        self.assertIn("pthread_join(image_capture_writer", runtime)
        self.assertNotIn("fallback síncrono", runtime)
        # CAMBIOS rev-av (afb6b0a): USER_2 es precondición del writer (si falla
        # la migración debe salir sin escribir) y el historial lo libera main,
        # nunca el callback del consumer GXM.
        self.assertIn("writer affinity USER_2 rc=0x%x; captura no escrita", runtime)
        affinity_at = runtime.index("affinity USER_2 rc=")
        store_at = runtime.index("job.written->store")
        self.assertLess(affinity_at, store_at)
        self.assertIn("return nullptr;", runtime[affinity_at:store_at])
        self.assertGreater(runtime.index("image_capture_frames.clear()"),
                           runtime.index("shutdown_d3d()"))


if __name__ == "__main__": unittest.main()
