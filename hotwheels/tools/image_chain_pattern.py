#!/usr/bin/env python3
"""Render a deterministic 800x600 test card through the Vita fit viewport model.

This is a host geometry/filter oracle, not a Vita/GXM readback or performance test.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw


SRC = (800, 600)
DST = (960, 544)


def make_pattern() -> Image.Image:
    image = Image.new("RGB", SRC, (24, 24, 24))
    draw = ImageDraw.Draw(image)
    # Border and one-pixel vertical/horizontal lines expose scaling and scissor.
    draw.rectangle((0, 0, 799, 599), outline=(255, 255, 255), width=1)
    draw.line((0, 300, 799, 300), fill=(255, 0, 0), width=1)
    draw.line((400, 0, 400, 599), fill=(0, 255, 0), width=1)
    # High frequency patch plus large color bars distinguish point/linear.
    for y in range(64, 192):
        for x in range(64, 192):
            v = 255 if ((x ^ y) & 1) else 0
            image.putpixel((x, y), (v, v, v))
    draw.rectangle((240, 64, 735, 190), fill=(0, 0, 0))
    colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255), (255, 255, 0)]
    for i, color in enumerate(colors):
        x0 = 250 + i * 96
        draw.rectangle((x0, 75, x0 + 79, 155), fill=color)
    # Pixel glyphs are deliberately drawn at source resolution.
    draw.rectangle((240, 210, 560, 250), fill=(0, 0, 0))
    draw.text((248, 220), "HW-032 800x600", fill=(255, 255, 255))
    # UI reference: a cyan 1px crosshair at the source center.
    draw.line((390, 300, 410, 300), fill=(0, 255, 255), width=1)
    draw.line((400, 290, 400, 310), fill=(0, 255, 255), width=1)
    return image


def viewport(src=SRC, dst=DST):
    sw, sh = src
    dw, dh = dst
    if min(sw, sh, dw, dh) <= 0:
        raise ValueError("dimensions must be positive")
    if dw * sh <= dh * sw:
        w, h = dw, (dw * sh) // sw
    else:
        w, h = (dh * sw) // sh, dh
    return ((dw - w) // 2, (dh - h) // 2, w, h)


def render(filter_name: str) -> Image.Image:
    x, y, w, h = viewport()
    resample = Image.Resampling.NEAREST if filter_name == "point" else Image.Resampling.BILINEAR
    scaled = make_pattern().resize((w, h), resample=resample)
    out = Image.new("RGB", DST, (0, 0, 0))
    out.paste(scaled, (x, y))
    # UI safe-area marker is fixed in native output coordinates, independent
    # of the 4:3 game viewport; it is cyan to distinguish it from scene content.
    draw = ImageDraw.Draw(out)
    draw.rectangle((8, 8, 31, 23), outline=(0, 255, 255), width=1)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--filter", choices=("point", "linear"), default="point")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    image = render(args.filter)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.output)
    print(f"size={image.width}x{image.height} viewport={viewport()} filter={args.filter} sha256={__import__('hashlib').sha256(args.output.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
