#!/usr/bin/env python3
"""Regresión de LiveArea: assets visibles y PNG indexado Vita."""
import struct
import contextlib
import os
import shutil
import unittest
import zipfile
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools/make_sce_sys.py"
EXPECTED = {
    "icon0.png": (128, 128),
    "livearea/contents/bg.png": (840, 500),
    "livearea/contents/startup.png": (280, 158),
}


def inspect_png(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    assert data[12:16] == b"IHDR", path
    width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", data[16:29])
    assert depth == 8 and color == 3, (path, depth, color)
    assert compression == filtering == interlace == 0
    plte = data.index(b"PLTE")
    colors = struct.unpack(">I", data[plte - 4:plte])[0] // 3
    return width, height, colors


def main():
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["python3", str(GENERATOR), "--out", tmp], check=True)
        for rel, size in EXPECTED.items():
            width, height, colors = inspect_png(Path(tmp) / rel)
            assert (width, height) == size, (rel, width, height)
            assert colors >= (6 if rel == "icon0.png" else 8), (rel, colors)
    print("test_sce_sys: PNG paleta 8 bits, dimensiones y assets visibles OK")


@contextlib.contextmanager
def original_fixture():
    from PIL import Image, ImageDraw
    (ROOT / "out").mkdir(exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix="sce-sys-test-", dir=ROOT / "out"))
    try:
        source = folder / "source"
        (source / "autostart").mkdir(parents=True)
        Image.new("RGB", (8, 8), (240, 0, 0)).save(source / "HWICON.BMP")
        background = Image.new("RGB", (100, 50), (0, 0, 240))
        draw = ImageDraw.Draw(background)
        for rect, color in [((0, 0, 19, 9), (240, 0, 0)),
                            ((80, 0, 99, 9), (0, 240, 0)),
                            ((0, 40, 19, 49), (240, 240, 0)),
                            ((80, 40, 99, 49), (0, 240, 240)),
                            ((0, 20, 9, 29), (240, 0, 240)),
                            ((90, 20, 99, 29), (240, 240, 240)),
                            ((45, 0, 54, 7), (240, 120, 0)),
                            ((45, 42, 54, 49), (120, 120, 120))]:
            draw.rectangle(rect, fill=color)
        background.save(source / "autostart/background.bmp")
        yield folder, source
    except BaseException:
        print("FAIL: evidencia en", folder)
        raise
    else:
        shutil.rmtree(folder)


class OriginalArtTests(unittest.TestCase):
    def run_generator(self, out, source):
        return subprocess.run(["python3", str(GENERATOR), "--out", str(out),
                               "--original-dir", str(source)], capture_output=True, text=True)

    def assert_cover(self, image, size):
        self.assertEqual(image.size, size)
        rgb = image.convert("RGB")
        content_height = size[0] // 2  # fixture 100x50, imagen entera sin deformar
        top = (size[1] - content_height) // 2
        self.assertEqual(rgb.getpixel((0, top - 1)), (0, 0, 0))
        self.assertEqual(rgb.getpixel((0, top + content_height)), (0, 0, 0))
        markers = [((.1, .1), (240, 0, 0)), ((.9, .1), (0, 240, 0)),
                   ((.1, .9), (240, 240, 0)), ((.9, .9), (0, 240, 240)),
                   ((.05, .5), (240, 0, 240)), ((.95, .5), (240, 240, 240)),
                   ((.5, .08), (240, 120, 0)), ((.5, .92), (120, 120, 120))]
        for (x, y), expected in markers:
            actual = rgb.getpixel((int(x * size[0]), top + int(y * content_height)))
            self.assertLessEqual(max(abs(a - b) for a, b in zip(actual, expected)), 3,
                                 (x, y, actual, expected))

    def test_original_images_and_proportions(self):
        from PIL import Image
        import xml.etree.ElementTree as ET
        with original_fixture() as (folder, source):
            out = folder / "art"
            result = self.run_generator(out, source)
            self.assertEqual(result.returncode, 0, result.stderr)
            for rel, size in EXPECTED.items():
                self.assertEqual(inspect_png(out / rel)[:2], size)
                with Image.open(out / rel) as image:
                    self.assertEqual(image.mode, "P")
                    rgb = image.convert("RGB")
                    self.assertEqual(rgb.getpixel((size[0] // 2, size[1] // 2)),
                                     (240, 0, 0) if rel == "icon0.png" else (0, 0, 240))
                    if rel != "icon0.png":
                        self.assert_cover(image, size)
                        self.assertEqual(rgb.getpixel((0, 0)), (0, 0, 0))
                        self.assertEqual(rgb.getpixel((0, size[1] - 1)), (0, 0, 0))
                        self.assertEqual(rgb.getpixel((0, size[1] // 2)), (240, 0, 240))
            xml = ET.parse(out / "livearea/contents/template.xml")
            self.assertEqual(xml.findtext("livearea-background/image"), "bg.png")
            self.assertEqual(xml.findtext("gate/startup-image"), "startup.png")
            repeat = folder / "repeat"
            self.assertEqual(self.run_generator(repeat, source).returncode, 0)
            for rel in EXPECTED:
                self.assertEqual((out / rel).read_bytes(), (repeat / rel).read_bytes())

    def test_icon_nearest_and_cover_smoothing(self):
        from PIL import Image
        with original_fixture() as (folder, source):
            icon = Image.new("RGB", (32, 32))
            values = [((i % 256), (i * 17) % 256, (i * 31) % 256) for i in range(1024)]
            icon.putdata(values)
            icon.save(source / "HWICON.BMP")
            out = folder / "art"
            result = self.run_generator(out, source)
            self.assertEqual(result.returncode, 0, result.stderr)
            with Image.open(out / "icon0.png") as enlarged:
                rgb = enlarged.convert("RGB")
                self.assertEqual(list(rgb.getdata()),
                    [values[(y // 4) * 32 + x // 4] for y in range(128) for x in range(128)])
            for rel in ("livearea/contents/bg.png", "livearea/contents/startup.png"):
                with Image.open(out / rel) as cover:
                    # La fixture tiene nueve colores planos; el filtro debe suavizar sus bordes.
                    self.assertGreater(len(set(cover.convert("RGB").getdata())), 12)

    def test_missing_source_fails_and_tracked_output_is_blocked(self):
        with original_fixture() as (folder, source):
            (source / "HWICON.BMP").unlink()
            self.assertNotEqual(self.run_generator(folder / "art", source).returncode, 0)
            before = {rel: (ROOT / "sce_sys" / rel).read_bytes() for rel in EXPECTED}
            result = self.run_generator(ROOT / "sce_sys", source)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("versionado", result.stderr)
            self.assertEqual(before, {rel: (ROOT / "sce_sys" / rel).read_bytes() for rel in EXPECTED})

    def test_package_auto_detection_is_unambiguous(self):
        from PIL import Image
        import io
        env = os.environ.copy()
        env.pop("HW_ORIGINAL_ART_DIR", None)
        env["VITASDK"] = str(ROOT.parent / "shared/vitasdk")
        if not (Path(env["VITASDK"]) / "bin/vita-pack-vpk").exists():
            self.skipTest("VitaSDK no disponible")
        with original_fixture() as (folder, source):
            fixture = folder / "project"
            tools = fixture / "tools"
            tools.mkdir(parents=True)
            for name in ("package.sh", "make_sce_sys.py", "app.conf"):
                shutil.copyfile(ROOT / "tools" / name, tools / name)
            out = folder / "package"
            out.mkdir()
            (out / "eboot.bin").write_bytes(b"synthetic eboot")
            command = ["bash", str(tools / "package.sh"), str(out)]
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("LiveArea genérico", result.stdout)
            local = fixture / "extracted/iso"
            local.mkdir(parents=True)
            shutil.copyfile(source / "HWICON.BMP", local / "HWICON.BMP")
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, "fuente parcial no debe generar genéricos")
            (local / "autostart").mkdir()
            shutil.copyfile(source / "autostart/background.bmp", local / "autostart/background.bmp")
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("LiveArea original", result.stdout)
            with zipfile.ZipFile(out / "hotwheels-vita.vpk") as vpk:
                with Image.open(io.BytesIO(vpk.read("sce_sys/icon0.png"))) as icon:
                    self.assertEqual(icon.convert("RGB").getpixel((64, 64)), (240, 0, 0))

    def test_package_original_and_explicit_generic(self):
        from PIL import Image
        import io
        env = os.environ.copy()
        env["PATH"] = str(ROOT.parent / "shared/vitasdk/bin") + os.pathsep + env["PATH"]
        if not shutil.which("vita-pack-vpk", path=env["PATH"]):
            self.skipTest("VitaSDK no disponible")
        with original_fixture() as (folder, source):
            out = folder / "package"
            out.mkdir()
            eboot = bytes(range(256)) * 4
            (out / "eboot.bin").write_bytes(eboot)
            before = {rel: (ROOT / "sce_sys" / rel).read_bytes() for rel in EXPECTED}
            env["HW_ORIGINAL_ART_DIR"] = str(source)
            result = subprocess.run(["bash", str(ROOT / "tools/package.sh"), str(out)],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with zipfile.ZipFile(out / "hotwheels-vita.vpk") as vpk:
                self.assertEqual(vpk.read("eboot.bin"), eboot)
                self.assertEqual(len(vpk.namelist()), 6)
                self.assertIn(b"HWMR00002", vpk.read("sce_sys/param.sfo"))
                for rel, size in EXPECTED.items():
                    data = vpk.read("sce_sys/" + rel)
                    with Image.open(io.BytesIO(data)) as image:
                        self.assertEqual(image.size, size)
                        self.assertEqual(image.mode, "P")
                        if rel == "icon0.png":
                            self.assertEqual(image.convert("RGB").getpixel((64, 64)), (240, 0, 0))
                        else:
                            self.assert_cover(image, size)
            self.assertEqual(before, {rel: (ROOT / "sce_sys" / rel).read_bytes() for rel in EXPECTED})
            self.assertEqual((out / "eboot.bin").read_bytes(), eboot)
            env["HW_ORIGINAL_ART_DIR"] = str(folder / "missing")
            result = subprocess.run(["bash", str(ROOT / "tools/package.sh"), str(out)],
                                    env=env, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            env["HW_ORIGINAL_ART_DIR"] = ""
            result = subprocess.run(["bash", str(ROOT / "tools/package.sh"), str(out)],
                                    env=env, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with zipfile.ZipFile(out / "hotwheels-vita.vpk") as vpk:
                self.assertEqual(vpk.read("sce_sys/icon0.png"), before["icon0.png"])


if __name__ == "__main__":
    main()
    unittest.main()
