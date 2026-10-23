#!/usr/bin/env python3
"""SPEC-053 CA1: instrucciones ARM ejecutadas desde un trace `qemu-arm -d nochain,exec,in_asm`.

qemu imprime un bloque `IN:` (con sus bytes) por TB traducido y una línea `Trace` por
TB EJECUTADO. Bajo `nochain` toda transición pasa por el bucle de TCG, así que el trace
es completo; contar los bytes del TB por ejecución da el número exacto de instrucciones
ARM ejecutadas (ARM de 32 bits: 4 bytes por instrucción).

La dirección de un TB NO identifica un bloque: el cajón de código del dynarec reusa
direcciones, así que la misma dirección puede alojar otro bloque (otro tamaño) más tarde.
Por eso cada ejecución se cuenta con el tamaño VIGENTE en ese momento; no se agrega por
dirección al final.

El log se consume en streaming (archivo o stdin) para no bajarlo a disco ni a RAM.
Una ejecución de un TB sin `in_asm` es un error (trace incompleto => subconteo), nunca un
valor silencioso.
"""
import argparse
import re
import sys

BLOCK_ADDR = re.compile(r"^(0x[0-9a-fA-F]+):\s*$")
TRACE = re.compile(r"^Trace \d+: 0x[0-9a-fA-F]+ \[[^/]*/([0-9a-fA-F]+)/")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", nargs="?", default="-", help="archivo del trace, o - para stdin")
    ap.add_argument("--per-tb", action="store_true", help="tabla por dirección, por instrucciones")
    args = ap.parse_args()
    src = sys.stdin if args.log == "-" else open(args.log, "r", errors="replace")

    sizes: dict[int, int] = {}      # dirección del TB -> instrucciones de la última versión cerrada
    execs: dict[int, int] = {}      # dirección del TB -> veces ejecutado
    insns_to: dict[int, int] = {}   # dirección del TB -> instrucciones acumuladas
    total = 0
    missing: set[int] = set()
    cur_addr: int | None = None
    cur_bytes = 0

    def close() -> None:
        nonlocal cur_addr, cur_bytes
        if cur_addr is not None and cur_bytes:
            sizes[cur_addr] = cur_bytes // 4
        cur_addr, cur_bytes = None, 0

    for raw in src:
        line = raw.rstrip("\n")
        if line.startswith("----------------") or line.startswith("IN:"):
            close()
        elif (m := BLOCK_ADDR.match(line)):
            cur_addr, cur_bytes = int(m.group(1), 16), 0
        elif line.startswith("OBJD-T:"):
            cur_bytes += len(line.split(":", 1)[1].strip()) // 2
        elif line.startswith("Trace "):
            m = TRACE.match(line)
            if not m:
                print(f"qemu_insn_count: línea Trace malformada: {line!r}", file=sys.stderr)
                return 1
            pc = int(m.group(1), 16)
            # El bloque en curso aún no se cerró; sus bytes ya están completos (qemu
            # imprime in_asm antes de ejecutar). Fuera de él, vale el último tamaño cerrado.
            size = cur_bytes // 4 if pc == cur_addr and cur_bytes else sizes.get(pc)
            if size is None:
                missing.add(pc)
                continue
            execs[pc] = execs.get(pc, 0) + 1
            insns_to[pc] = insns_to.get(pc, 0) + size
            total += size
    close()

    if missing:
        print(f"qemu_insn_count: {len(missing)} TB ejecutados sin in_asm "
              f"(trace incompleto; ¿falta -d nochain?); primero 0x{next(iter(missing)):x}",
              file=sys.stderr)
        return 1

    print(f"qemu-insn: tb_unique={len(sizes)} tb_exec={sum(execs.values())} "
          f"arm_insns={total} blocks_unknown=0")
    if args.per_tb:
        for pc in sorted(execs, key=lambda p: insns_to[p], reverse=True):
            print(f"0x{pc:08x} {execs[pc]} {insns_to[pc]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
