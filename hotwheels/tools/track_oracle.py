#!/usr/bin/env python3
"""track_oracle.py — HW-067 (SPEC-046): oráculo de render por pista para BUG-047.

  track_oracle.py identity <log> <MUNDO>     verifica que el log abrió DATA\\<MUNDO>\\<MUNDO>.SFP y ningún otro mundo
  track_oracle.py stats <run.log> [base.log] agrega líneas ringstat; con base (playa) marca ANOMALA/OK
  track_oracle.py diag <run.log>             imprime cada línea `[hw] diag` (SPEC-052) con sus claves

Logs aceptados: stdout/run.log de qemu (HW_TRACE=CreateFileA, HW_D3D_STATS=1) o traza +file de Wine.
"""
import re
import sys

WORLDS = ("WAVE", "SLUMBER", "OFFICE")
# DATA\<MUNDO>\<MUNDO>.SFP con separadores \ (qemu), \\ (Wine escapa) o / (inotify); DATA como componente entero.
_SFP = re.compile(r"(?<![A-Za-z0-9_])DATA[\\/]+(WAVE|SLUMBER|OFFICE)[\\/]+\1\.SFP", re.IGNORECASE)
_KV = re.compile(r"(\w+)=([^\s]+)")


def worlds_opened(text):
    """Mundos cuyo <MUNDO>.SFP aparece abierto, en orden de primera aparición."""
    out = []
    for m in _SFP.finditer(text):
        w = m.group(1).upper()
        if w not in out:
            out.append(w)
    return out


def verify_identity(text, world):
    return worlds_opened(text) == [world.upper()]


def aggregate(text):
    scenes = verts = bad = 0
    max_n = 0
    edge_max = 0.0
    edges = []
    for line in text.splitlines():
        if "ringstat " not in line:
            continue
        kv = dict(_KV.findall(line))
        scenes += 1
        verts += int(kv["verts"])
        bad += int(kv["oob"]) + int(kv["nan"])
        max_n = max(max_n, int(kv["max_n"]))
        e = float(kv["edge"])
        edges.append(e)
        edge_max = max(edge_max, e)
    edges.sort()
    p99 = edges[min(len(edges) - 1, int(len(edges) * 0.99))] if edges else 0.0
    return {"scenes": scenes, "verts": verts, "bad": bad, "bad_rate": bad / verts if verts else 0.0,
            "max_n": max_n, "edge_max": edge_max, "edge_p99": p99}


def diag_lines(text):
    """Una fila por línea `[hw] diag` (claves del contrato SPEC-052), en orden."""
    return [dict(_KV.findall(line.split("[hw] diag ", 1)[1]))
            for line in text.splitlines() if "[hw] diag " in line]


def diag_fields(text):
    """Dict de la primera línea `[hw] diag`, o {} si no hay ninguna."""
    rows = diag_lines(text)
    return rows[0] if rows else {}


def anomalous(run, base, abs_margin=1e-3):
    """Pista anómala si su tasa de vértices malos supera la de la playa por abs_margin."""
    return run["bad_rate"] > base["bad_rate"] + abs_margin


def _read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def main(argv):
    if len(argv) >= 3 and argv[0] == "identity":
        text = _read(argv[1])
        ok = verify_identity(text, argv[2])
        print(f"identity pedido={argv[2].upper()} abiertos={','.join(worlds_opened(text)) or '-'} {'OK' if ok else 'INVALIDA'}")
        return 0 if ok else 1
    if len(argv) >= 2 and argv[0] == "diag":
        rows = diag_lines(_read(argv[1]))
        for r in rows:
            print(" ".join(f"{k}={v}" for k, v in r.items()))
        print(f"diag lineas={len(rows)}")
        return 0 if rows else 1
    if len(argv) >= 2 and argv[0] == "stats":
        run = aggregate(_read(argv[1]))
        print(" ".join(f"{k}={v:.6g}" if isinstance(v, float) else f"{k}={v}" for k, v in run.items()))
        if len(argv) >= 3:
            base = aggregate(_read(argv[2]))
            print("ANOMALA" if anomalous(run, base) else "OK", f"(base bad_rate={base['bad_rate']:.6g})")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
