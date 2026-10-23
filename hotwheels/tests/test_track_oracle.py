"""HW-067 SPEC-046 CA1b/CA3: identidad de pista por I/O y agregado de ringstat."""
import importlib.util
import pathlib
import unittest

HW = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("track_oracle", HW / "tools" / "track_oracle.py")
to = importlib.util.module_from_spec(spec)
spec.loader.exec_module(to)

QEMU = r'''
KERNEL32.CreateFileA("data\menu\objects.sfp",0x80000000) = 0x00000044
KERNEL32.CreateFileA("DATA\SLUMBER\VEHICLES.SFP",0x80000000) = 0x00000048
KERNEL32.CreateFileA("data\slumber\slumber.sfp",0x80000000) = 0x0000004c
'''
WINE = r'''trace:file:NtCreateFile handle=0x1c access=80100080 name=L"\\??\\C:\\Games\\Hot Wheels - Micro Racers\\DATA\\OFFICE\\OFFICE.SFP" objattr=00000040'''


def stat(scene, verts, oob=0, nan=0, edge=50.0, max_n=1000):
    return (f"[hw] ringstat w=800 h=600 draws=10 verts={verts} tris=1 trunc=0 nan={nan} oob={oob} rhw_le0=0 "
            f"z_out=0 x=0.0..800.0 y=0.0..600.0 z=0.0000..1.0000 rhw=1.0000..1.0000 edge={edge} uploads=0 "
            f"up_max=0x0 max_n={max_n} max_op=7 scene={scene}")


class Identity(unittest.TestCase):
    def test_qemu_trace(self):
        self.assertEqual(to.worlds_opened(QEMU), ["SLUMBER"])

    def test_wine_trace(self):
        self.assertEqual(to.worlds_opened(WINE), ["OFFICE"])

    def test_inotify_log(self):   # tools/wine/fwatch.c (opens.log)
        log = "OPEN /x/Hot Wheels - Micro Racers/DATA/SLUMBER/SLUMBER.SFP\nOPEN /x/DATA/SLUMBER/VEHICLES.SFP\n"
        self.assertEqual(to.worlds_opened(log), ["SLUMBER"])

    def test_requires_data_component(self):   # review-7d87219 P2#3
        self.assertFalse(to.verify_identity("OPEN /tmp/SLUMBER/SLUMBER.SFP\n", "SLUMBER"))
        self.assertFalse(to.verify_identity("OPEN /x/MYDATA/SLUMBER/SLUMBER.SFP\n", "SLUMBER"))
        self.assertTrue(to.verify_identity("OPEN /x/DATA/SLUMBER/SLUMBER.SFP\n", "SLUMBER"))

    def test_verify(self):
        self.assertTrue(to.verify_identity(QEMU, "slumber"))
        self.assertFalse(to.verify_identity(QEMU, "WAVE"))           # pista equivocada
        self.assertFalse(to.verify_identity(QEMU + WINE, "SLUMBER"))  # dos mundos: ambigua
        self.assertFalse(to.verify_identity("", "WAVE"))


class Aggregate(unittest.TestCase):
    def test_aggregate(self):
        log = "\n".join([stat(1, 1000), stat(2, 1000, oob=3, nan=1, edge=900.0, max_n=70000), "[hw] otra cosa"])
        a = to.aggregate(log)
        self.assertEqual(a["scenes"], 2)
        self.assertEqual(a["verts"], 2000)
        self.assertEqual(a["bad"], 4)
        self.assertAlmostEqual(a["bad_rate"], 4 / 2000)
        self.assertEqual(a["max_n"], 70000)
        self.assertEqual(a["edge_max"], 900.0)

    def test_anomalous_vs_beach(self):
        beach = to.aggregate("\n".join(stat(i, 1000) for i in range(10)))
        same = to.aggregate("\n".join(stat(i, 1000, oob=0) for i in range(10)))
        bad = to.aggregate("\n".join(stat(i, 1000, oob=20) for i in range(10)))
        self.assertFalse(to.anomalous(same, beach))
        self.assertTrue(to.anomalous(bad, beach))

    def test_empty(self):
        a = to.aggregate("")
        self.assertEqual(a["scenes"], 0)
        self.assertEqual(a["bad_rate"], 0.0)


class TrackSettings(unittest.TestCase):
    """CA1: race-d3d-track<N>.dat = base con línea 5 (=1 carrera simple), 6 (pista, 0-based) y 18 (=2 desbloqueadas)."""
    def test_files(self):
        base = (HW / "tools" / "race-d3d-settings.dat").read_text().split()
        for n in (1, 2, 3):
            lines = (HW / "tools" / f"race-d3d-track{n}.dat").read_text().split()
            self.assertEqual(len(lines), 18)
            self.assertTrue(all(x.lstrip("-").isdigit() for x in lines))
            self.assertEqual(lines[4], "1")
            self.assertEqual(lines[5], str(n - 1))
            self.assertEqual(lines[17], "2")
            diff = [i for i, (a, b) in enumerate(zip(base, lines)) if a != b]
            self.assertTrue(set(diff) <= {4, 5, 17}, diff)


if __name__ == "__main__":
    unittest.main()
