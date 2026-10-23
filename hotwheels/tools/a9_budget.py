#!/usr/bin/env python3
"""HW-073 (perf-host) SPEC-054: presupuesto A9 del resampleo de voz (ds_emul).

El número medido es INSTRUCCIONES DINÁMICAS por frame de salida (qemu-arm con
-singlestep -d exec; ver tools/a9/run_a9_budget.sh). qemu NO modela el pipeline
del A9: este módulo convierte instrucciones a una fracción de core usando un CPI
declarado explícitamente (default 1.0, in-order dual-issue con mezcla escalar/
NEON). No es una medición de ciclos en consola.

  insn_s = insn_frame · out_rate · voices
  cores  = insn_s · CPI / 444e6            (reloj de release, runtime_vita.cpp)
"""
import argparse

A9_HZ = 444_000_000.0  # reloj ARM objetivo en consola (release, scePower 444 MHz)


def budget(insn_frame, out_rate, voices, cpi=1.0):
    """Insn/frame de una voz → instrucciones/s, ciclos/s (modelo) y fracción de core."""
    insn_s = insn_frame * out_rate * voices
    cycles_s = insn_s * cpi
    cores = cycles_s / A9_HZ
    return {
        "insn_frame": insn_frame,
        "insn_s": insn_s,
        "cycles_s": cycles_s,
        "cores": cores,
        "cores_milli": int(round(cores * 1000)),
    }


def coeff_bytes(taps, banks=32, phases=256):
    """Bytes de la tabla Q15 de coeficientes (int16) del kernel sinc: banks·fases·taps·2."""
    return banks * phases * taps * 2


def fits_l1d(taps, banks_active=1, l1d=32 * 1024):
    """¿Cabizcan en L1D las tablas de las voces activas? El A9 tiene 32 KiB L1D por core."""
    return coeff_bytes(taps) // 32 * banks_active <= l1d


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--insn-frame", type=float, required=True, help="instrucciones por frame de salida, por voz")
    p.add_argument("--rate", type=int, default=48000, help="frames de salida por segundo (48 kHz)")
    p.add_argument("--voices", type=int, required=True)
    p.add_argument("--cpi", type=float, default=1.0)
    p.add_argument("--taps", type=int, default=0, help="si >0, agrega el footprint de la tabla sinc")
    a = p.parse_args()
    b = budget(a.insn_frame, a.rate, a.voices, a.cpi)
    fields = [f"core=a9", f"insn_frame={a.insn_frame:g}", f"rate={a.rate}", f"voices={a.voices}",
              f"cpi={a.cpi:g}", f"insn_s={b['insn_s']:.0f}", f"cycles_s={b['cycles_s']:.0f}",
              f"cores={b['cores']:.4f}", f"cores_milli={b['cores_milli']}"]
    if a.taps:
        fields.append(f"coeff_kib={coeff_bytes(a.taps) / 1024:.0f}")
        fields.append(f"fits_l1d={int(fits_l1d(a.taps))}")
    print(" ".join(fields))


if __name__ == "__main__":
    main()
