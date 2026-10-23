#!/usr/bin/env python3
"""Verifica viewport 960x544 del fixture P8 en captura root del harness."""
import sys
from PIL import Image


def check(path):
    img = Image.open(path).convert('RGB')
    assert img.width >= 960 and img.height >= 544, 'captura demasiado chica'
    row = 100  # fuera del log/UI de Vita3K y dentro del viewport
    assert all(img.getpixel((x, row)) == (0, 0, 0)
               for x in list(range(117)) + list(range(842, 960))), 'barras laterales'
    colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0),
              (255, 0, 255), (0, 255, 255), (255, 255, 255), (128, 64, 32)]
    for i, expected in enumerate(colors):
        actual = img.getpixel((117 + int((i + .5) * 725 / 8), row))
        accepted = [expected, (128, 0, 0)] if i == 0 else [expected]
        assert actual in accepted, f'franja {i}: {actual}, esperado {accepted}'
    print('P8 capture: PASS (8 franjas; barras 117/118; viewport 960x544)')


if __name__ == '__main__':
    check(sys.argv[1])
