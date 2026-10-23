#!/usr/bin/env python3
"""Comparación reproducible de dos capturas RGB alineadas."""
import argparse
from PIL import Image, ImageFilter, ImageChops, ImageStat
import numpy as np


def compare_images(a, b):
    a, b = a.convert("RGB"), b.convert("RGB")
    if a.size != b.size:
        raise ValueError("dimensiones distintas: %s vs %s" % (a.size, b.size))
    mae = sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3
    la = a.convert("L").filter(ImageFilter.FIND_EDGES)
    lb = b.convert("L").filter(ImageFilter.FIND_EDGES)
    gradient_reference = ImageStat.Stat(la).mean[0]
    gradient_candidate = ImageStat.Stat(lb).mean[0]
    gradient_mae = ImageStat.Stat(ImageChops.difference(la, lb)).mean[0]
    def lap_var(image):
        gray = np.asarray(image.convert("L"), dtype=np.float64)
        lap = (gray[1:-1, :-2] + gray[1:-1, 2:] + gray[:-2, 1:-1] + gray[2:, 1:-1]
               - 4 * gray[1:-1, 1:-1])
        return float(lap.var())
    return {"mae": mae, "gradient_mean_reference": gradient_reference,
            "gradient_mean_candidate": gradient_candidate, "gradient_mae": gradient_mae,
            "laplacian_variance_reference": lap_var(a),
            "laplacian_variance_candidate": lap_var(b)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("reference")
    p.add_argument("candidate")
    p.add_argument("--crop", help="ROI x,y,w,h común; delimita el viewport dentro del screenshot")
    args = p.parse_args()
    reference, candidate = Image.open(args.reference), Image.open(args.candidate)
    if args.crop:
        x, y, w, h = (int(n) for n in args.crop.split(","))
        if min(x, y) < 0 or min(w, h) <= 0:
            p.error("--crop requiere x,y>=0 y w,h>0")
        box = (x, y, x + w, y + h)
        if box[2] > reference.width or box[3] > reference.height or box[2] > candidate.width or box[3] > candidate.height:
            p.error("ROI fuera de captura")
        reference, candidate = reference.crop(box), candidate.crop(box)
    values = compare_images(reference, candidate)
    print("MAE RGB: %.3f/255" % values["mae"])
    print("Gradiente medio referencia/candidato: %.3f / %.3f /255" %
          (values["gradient_mean_reference"], values["gradient_mean_candidate"]))
    print("MAE gradiente entre imágenes: %.3f/255" % values["gradient_mae"])
    print("Varianza Laplaciano referencia/candidato: %.3f / %.3f (mayor = más detalle o aliasing)" %
          (values["laplacian_variance_reference"], values["laplacian_variance_candidate"]))


if __name__ == "__main__": main()
