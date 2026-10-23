#!/usr/bin/env python3
# hotwheels/tools/make_sce_sys.py — genera los assets de LiveArea/icono (HW-017).
#
# Por defecto se dibuja con stdlib; --original-dir usa BMP locales del usuario
# con Pillow y salida fuera del sce_sys versionado. La salida es determinista (sin timestamps,
# filtro PNG 0, zlib nivel fijo) para que tests/test_vpk.sh pueda comparar
# byte a byte el árbol commiteado contra lo que genera el script.
#
# Uso:
#   make_sce_sys.py [--out DIR]      # DIR default: hotwheels/sce_sys
#
# Sizes de LiveArea (Vita):
#   icon0.png    128x128
#   startup.png  280x158
#   bg.png       840x500

import argparse
import os
import struct
import sys
import zlib

# ---- escritura PNG (RGBA8, sin filtro, determinista) ------------------------

def _chunk(tag: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path: str, w: int, h: int, px: bytearray) -> None:
    # La Vita exige PNG de LiveArea con paleta de 8 bits (RGBA → error 0x8010113D al instalar).
    assert len(px) == w * h * 4
    shift = 0
    while True:  # cuantiza bajando bits por canal hasta que entre en 256 colores
        m = 0xFF & ~((1 << shift) - 1)
        cols = {(px[i] & m, px[i + 1] & m, px[i + 2] & m) for i in range(0, len(px), 4)}
        if len(cols) <= 256:
            break
        shift += 1
    pal = sorted(cols)
    idx = {c: n for n, c in enumerate(pal)}
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filtro "None"
        for x in range(w):
            i = (y * w + x) * 4
            raw.append(idx[(px[i] & m, px[i + 1] & m, px[i + 2] & m)])
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)  # 8 bits indexado
    blob = (b"\x89PNG\r\n\x1a\n"
            + _chunk(b"IHDR", ihdr)
            + _chunk(b"PLTE", bytes(v for c in pal for v in c))
            + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + _chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(blob)


# ---- primitivas de dibujo ---------------------------------------------------

class Canvas:
    def __init__(self, w: int, h: int, rgba=(0, 0, 0, 255)):
        self.w, self.h = w, h
        self.px = bytearray(bytes(rgba) * (w * h))

    def set(self, x: int, y: int, rgba):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 4
            self.px[i:i + 4] = bytes(rgba)

    def rect(self, x0, y0, x1, y1, rgba):
        for y in range(max(0, y0), min(self.h, y1)):
            for x in range(max(0, x0), min(self.w, x1)):
                self.set(x, y, rgba)

    def checker(self, x0, y0, x1, y1, size, c0, c1):
        for y in range(max(0, y0), min(self.h, y1)):
            for x in range(max(0, x0), min(self.w, x1)):
                self.set(x, y, c0 if ((x // size) + (y // size)) % 2 == 0 else c1)

    def vgrad(self, x0, y0, x1, y1, c0, c1):
        run = max(1, y1 - y0 - 1)
        for y in range(max(0, y0), min(self.h, y1)):
            t = (y - y0) / run
            col = tuple(round(c0[i] + (c1[i] - c0[i]) * t) for i in range(3)) + (255,)
            for x in range(max(0, x0), min(self.w, x1)):
                self.set(x, y, col)


# ---- paleta propia (nada del juego) -----------------------------------------
INK = (16, 24, 32, 255)        # azul-casi-negro
WHITE = (238, 240, 242, 255)
RED = (255, 72, 42, 255)
STEEL0 = (9, 19, 38, 255)
STEEL1 = (21, 91, 137, 255)
CYAN = (47, 218, 226, 255)
YELLOW = (255, 205, 56, 255)
TIRE = (5, 9, 16, 255)


def _poly(c: Canvas, points, color):
    ymin = max(0, min(y for _, y in points))
    ymax = min(c.h - 1, max(y for _, y in points))
    for y in range(ymin, ymax + 1):
        xs = []
        for (x0, y0), (x1, y1) in zip(points, points[1:] + points[:1]):
            if y0 == y1 or not (min(y0, y1) <= y < max(y0, y1)):
                continue
            xs.append(round(x0 + (y - y0) * (x1 - x0) / (y1 - y0)))
        xs.sort()
        for left, right in zip(xs[::2], xs[1::2]):
            c.rect(left, y, right + 1, y + 1, color)


def _circle(c: Canvas, cx, cy, radius, color):
    for y in range(max(0, cy - radius), min(c.h, cy + radius + 1)):
        dx = int((radius * radius - (y - cy) ** 2) ** 0.5)
        c.rect(cx - dx, y, cx + dx + 1, y + 1, color)


def _text(c: Canvas, value, x, y, scale, color):
    glyphs = {
        "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
        "W": ("10001", "10001", "10001", "10101", "10101", "10101", "01010"),
    }
    for letter in value:
        rows = glyphs[letter]
        for gy, row in enumerate(rows):
            for gx, bit in enumerate(row):
                if bit == "1":
                    c.rect(x + gx * scale, y + gy * scale,
                           x + (gx + 1) * scale, y + (gy + 1) * scale, color)
        x += 6 * scale


def _car(c: Canvas, cx, y, width, height):
    left, right = cx - width // 2, cx + width // 2
    # Silueta genérica de auto de carreras, creada para este port.
    _poly(c, [(left, y + height * 3 // 4), (left + width // 8, y + height // 2),
              (left + width * 3 // 8, y + height // 2), (cx - width // 5, y + height // 5),
              (cx + width // 5, y + height // 5), (right - width // 4, y + height // 2),
              (right, y + height * 3 // 5), (right, y + height * 4 // 5),
              (left, y + height * 4 // 5)], YELLOW)
    _poly(c, [(cx - width // 6, y + height // 3), (cx - width // 12, y + height // 4),
              (cx + width // 7, y + height // 4), (cx + width // 4, y + height // 2),
              (cx - width // 5, y + height // 2)], CYAN)
    c.rect(left + width // 9, y + height * 2 // 3,
           right - width // 10, y + height * 2 // 3 + max(2, height // 12), RED)
    for wx in (left + width // 4, right - width // 4):
        _circle(c, wx, y + height * 4 // 5, height // 5, TIRE)
        _circle(c, wx, y + height * 4 // 5, max(2, height // 11), WHITE)


def make_icon() -> Canvas:
    c = Canvas(128, 128, INK)
    c.vgrad(5, 5, 123, 123, STEEL0, STEEL1)
    c.rect(0, 0, 128, 5, RED); c.rect(0, 123, 128, 128, RED)
    c.rect(0, 0, 5, 128, RED); c.rect(123, 0, 128, 128, RED)
    _text(c, "HW", 26, 20, 9, WHITE)
    _car(c, 64, 78, 100, 39)
    return c


def make_bg() -> Canvas:
    c = Canvas(840, 500, STEEL0)
    c.vgrad(0, 0, 840, 500, STEEL0, STEEL1)
    # Bandas cinéticas, insignia HW y auto propios sobre pista abstracta.
    _poly(c, [(0, 355), (840, 250), (840, 330), (0, 435)], (12, 39, 66, 255))
    _poly(c, [(0, 382), (840, 277), (840, 292), (0, 407)], CYAN)
    c.checker(0, 435, 840, 483, 24, WHITE, INK)
    c.rect(74, 54, 365, 235, INK)
    c.rect(81, 61, 358, 228, YELLOW)
    _text(c, "HW", 112, 84, 19, INK)
    _car(c, 580, 265, 390, 155)
    c.rect(0, 0, 840, 6, RED)
    return c


def make_startup() -> Canvas:
    c = Canvas(280, 158, STEEL0)
    c.vgrad(0, 0, 280, 158, STEEL0, STEEL1)
    c.checker(0, 128, 280, 154, 13, WHITE, INK)
    c.rect(16, 18, 119, 96, YELLOW)
    _text(c, "HW", 27, 32, 9, INK)
    _car(c, 203, 75, 133, 53)
    c.rect(0, 0, 280, 4, RED)
    return c


ASSETS = [
    ("icon0.png", make_icon),
    ("livearea/contents/bg.png", make_bg),
    ("livearea/contents/startup.png", make_startup),
]

TEMPLATE_XML = """<?xml version="1.0" encoding="utf-8"?>

<livearea style="a1" format-ver="01.00" content-rev="1">
\t<livearea-background>
\t\t<image>bg.png</image>
\t</livearea-background>

\t<gate>
\t\t<startup-image>startup.png</startup-image>
\t</gate>
</livearea>
"""


def generate_original(out_dir: str, original_dir: str) -> None:
    tracked = os.path.realpath(os.path.join(os.path.dirname(__file__), "..", "sce_sys"))
    if os.path.commonpath([tracked, os.path.realpath(out_dir)]) == tracked:
        raise ValueError("salida original dentro del sce_sys versionado; usá --out en build/out")
    from PIL import Image, ImageOps
    source_paths = [os.path.join(original_dir, "HWICON.BMP"),
                    os.path.join(original_dir, "autostart", "background.bmp")]
    # Validar ambas antes de escribir para no producir una mezcla de modos.
    with Image.open(source_paths[0]) as icon, Image.open(source_paths[1]) as background:
        images = [icon.convert("RGB"), background.convert("RGB")]
    for rel, size, source, method in [
        ("icon0.png", (128, 128), images[0], Image.Resampling.NEAREST),
        ("livearea/contents/bg.png", (840, 500), images[1], Image.Resampling.LANCZOS),
        ("livearea/contents/startup.png", (280, 158), images[1], Image.Resampling.LANCZOS),
    ]:
        fitted = ImageOps.contain(source, size, method=method)
        canvas = Image.new("RGB", size, (0, 0, 0))
        canvas.paste(fitted, ((size[0] - fitted.width) // 2, (size[1] - fitted.height) // 2))
        indexed = canvas.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
        target = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        indexed.save(target, format="PNG", bits=8, optimize=False, compress_level=9)


def generate(out_dir: str, original_dir=None) -> None:
    if original_dir is not None:
        generate_original(out_dir, original_dir)
    else:
        for rel, fn in ASSETS:
            canvas = fn()
            write_png(os.path.join(out_dir, rel), canvas.w, canvas.h, canvas.px)
    tpl = os.path.join(out_dir, "livearea/contents/template.xml")
    os.makedirs(os.path.dirname(tpl), exist_ok=True)
    with open(tpl, "w", newline="\n") as f:
        f.write(TEMPLATE_XML)


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    default = os.path.join(here, "..", "sce_sys")
    ap = argparse.ArgumentParser(description="Genera sce_sys/ de hotwheels (HW-017)")
    ap.add_argument("--out", default=os.path.normpath(default),
                    help="directorio de salida (default: hotwheels/sce_sys)")
    ap.add_argument("--original-dir", help="DIR local con HWICON.BMP y autostart/background.bmp (requiere Pillow)")
    args = ap.parse_args()
    try:
        generate(args.out, args.original_dir)
    except (OSError, ValueError, ImportError) as error:
        ap.error(str(error))
    print("OK sce_sys generado en %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
