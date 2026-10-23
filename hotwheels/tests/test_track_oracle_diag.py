"""HW-068 SPEC-052 CA4: la línea `[hw] diag` la parsea tools/track_oracle.py."""
import importlib.util
import pathlib
import unittest

HW = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("track_oracle", HW / "tools" / "track_oracle.py")
to = importlib.util.module_from_spec(spec)
spec.loader.exec_module(to)

DIAG = ("[hw] diag context=office window_ms=10000 ring_bad=7 ring_verts=1000 ring_maxn=32776 ring_edge=141.4 "
        "vgl_ram_free_min=1234 vgl_vram_free_min=5678 circ_peak=524288 circ_overrun=2 circ_wraps=3 circ_slot=1 "
        "circ_off=4096 vgl_alloc_fail=4 gl_err=5 tex_live=42 tex_bytes=1048576 tex_up=88 tex_del_inflight=6 "
        "q_dropped=9 q_carried=10 canary_bad=11 pool_enter_ram=24000 pool_enter_vram=64000")


class DiagLine(unittest.TestCase):
    def test_fields(self):
        d = to.diag_fields(DIAG)
        self.assertEqual(d["context"], "office")
        self.assertEqual(d["ring_bad"], "7")
        self.assertEqual(d["tex_del_inflight"], "6")
        self.assertEqual(to.diag_fields("[hw] otra cosa"), {})

    def test_lines_ignores_non_diag(self):
        log = "\n".join([DIAG, "[hw] fps-window mode=original", DIAG.replace("ring_bad=7", "ring_bad=9")])
        rows = to.diag_lines(log)
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["ring_bad"], "7")
        self.assertEqual(rows[1]["ring_bad"], "9")


if __name__ == "__main__":
    unittest.main()
