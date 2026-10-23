#!/usr/bin/env python3
"""SPEC-053 CA3: costo por función a partir del conteo ARM y la distribución EIP.

Reparte las instrucciones ARM ejecutadas (exactas, de qemu_insn_count.py) según el
porcentaje por función del perfil EIP (HW-051) y estima ciclos/frame en la A9 con un
`cycles_per_insn` explícito. Es una ESTIMACIÓN de trabajo, no un tiempo de consola:
el reparto por función asume `muestras ∝ ciclos` y el CPI es uniforme (no modela
stalls de memoria ni VFP); por eso el CPI y la frecuencia son parámetros visibles.
"""
import argparse
import re
import sys

COUNTS = re.compile(r"qemu-insn:.*\barm_insns=(\d+)")
GAME = re.compile(r"pcprof-game scene=(\S+) rank=(\d+) eip=(0x[0-9a-fA-F]+) name=(.*?) "
                  r"samples=(\d+) pct=([0-9.]+)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--counts", required=True, help="salida de qemu_insn_count.py")
    ap.add_argument("--profile", required=True, help="salida de HW-051 (pcprof)")
    ap.add_argument("--scene", required=True)
    ap.add_argument("--frames", type=int, required=True)
    ap.add_argument("--cpi", type=float, default=2.0, help="ciclos por instrucción (A9, estimación)")
    ap.add_argument("--clock-mhz", type=float, default=444.0)
    ap.add_argument("--top", type=int, default=30)
    args = ap.parse_args()

    if args.frames <= 0:
        print(f"arm_cost_model: --frames debe ser > 0 (recibido {args.frames})", file=sys.stderr)
        return 1

    total_arm = None
    for line in open(args.counts, errors="replace"):
        if (m := COUNTS.search(line)):
            total_arm = int(m.group(1))
            break
    if total_arm is None:
        print("arm_cost_model: no encontré la línea 'qemu-insn: ... arm_insns=' en --counts",
              file=sys.stderr)
        return 1

    rows = []
    for line in open(args.profile, errors="replace"):
        m = GAME.search(line)
        if m and m.group(1) == args.scene:
            rows.append((int(m.group(3), 16), m.group(4), int(m.group(5)), float(m.group(6))))
    if not rows:
        print(f"arm_cost_model: no hay líneas pcprof-game para scene={args.scene}",
              file=sys.stderr)
        return 1

    arm_per_frame = total_arm / args.frames
    cycles_per_frame = arm_per_frame * args.cpi
    clock_hz = args.clock_mhz * 1e6
    pct_sum = sum(r[3] for r in rows)
    rows.sort(key=lambda r: r[3], reverse=True)

    print(f"cost-model: scene={args.scene} frames={args.frames} arm_insns={total_arm} "
          f"arm_insns_per_frame={arm_per_frame:.1f} cpi={args.cpi:.2f} clock_mhz={args.clock_mhz:g} "
          f"est_cycles_per_frame={cycles_per_frame:.1f} est_frame_ms={1000 * cycles_per_frame / clock_hz:.3f} "
          f"est_fps_ceiling={clock_hz / cycles_per_frame:.1f} profile_pct_sum={pct_sum:.2f}")
    for eip, name, _samples, pct in rows[:args.top]:
        insns = arm_per_frame * pct / 100.0
        print(f"  0x{eip:08x} {name} instr_per_frame={insns:.1f} "
              f"cycles_per_frame={insns * args.cpi:.1f} pct={pct:.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
