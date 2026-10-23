#!/usr/bin/env python3
"""CA1 SPEC-055: auditoría del status word x87 para el HLE de `__ftol`.

Responde, sobre el desensamblado real del exe:
  (a) ¿la secuencia de `__ftol` deja el control word neto nulo (FSTCW + FLDCW de
      restauración)?  → habilita reconocer el patrón completo en el codegen.
  (b) ¿cuántos lectores del status word tiene el invitado (FNSTSW/FSTSW/FNSTENV)?
  (c) por cada llamada a `__ftol`: ¿el `#IA` que puede levantar su `FISTP` puede
      ser leído antes de que otra instrucción x87 sobrescriba los condition codes
      C0/C2/C3?  ¿y el flag sticky IE antes de que algo lo limpie?

Semántica del status word x87 (Intel SDM):
  - C0 (bit 8), C2 (bit 10), C3 (bit 14): los escribe la última comparación
    (FCOM*/FUCOM*) o conversión a entero (FIST/FISTP/FISTTP).
  - IE (bit 0), DE, ZE, OE, UF, SF, XF: flags sticky de excepción; los limpia
    FNCLEX/FCLEX/FLDENV/FRSTOR/FNINIT.
  - FNSTSW/FSTSW copian la palabra entera → ven ambos.

Uso:
  tools/ftol_status_audit.py [--exe RUTA] [--asm RUTA] [--json]
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / "hotwheels/extracted/original/Program_Files/HotWheels.exe"

FTOL_VA = 0x4521C4

# Lectores del status word (copian la palabra de 16 bits).
STATUS_READERS = {"fnstsw", "fstsw", "fnstenv", "fstenv"}
# Instrucciones que escriben C0/C2/C3 (comparación o conversión a entero).
CONDITION_WRITERS = {
    "fcom", "fcomp", "fcompp", "fucom", "fucomp", "fucompp",
    "fist", "fistp", "fisttp",
}
# Instrucciones que limpian los flags sticky de excepción.
STICKY_CLEARERS = {"fnclex", "fclex", "fldenv", "frstor", "fninit", "finit"}
# Sufijos de tamaño que no cambian la clase de la instrucción.
SIZE_SUFFIXES = ("ll", "l", "s", "t", "p")


def base_mnemonic(mnemonic: str) -> str:
    """Quita el sufijo de tamaño: fistpl -> fistp, fucomip -> fucomi."""
    m = mnemonic.lower()
    if m in CONDITION_WRITERS or m in STATUS_READERS or m in STICKY_CLEARERS:
        return m
    for suffix in SIZE_SUFFIXES:
        if m.endswith(suffix) and m[: -len(suffix)] in CONDITION_WRITERS:
            return m[: -len(suffix)]
    return m


class Insn:
    __slots__ = ("addr", "mnemonic", "operands")

    def __init__(self, addr: int, mnemonic: str, operands: str):
        self.addr = addr
        self.mnemonic = mnemonic
        self.operands = operands

    def __repr__(self) -> str:  # pragma: no cover - sólo para depurar
        return f"0x{self.addr:x} {self.mnemonic} {self.operands}".rstrip()


_LINE = re.compile(r"^\s*([0-9a-f]+):\s")


def parse_disasm(text: str):
    """Parsea la salida de `objdump -d`: dirección, mnemónico y operandos."""
    insns = []
    for raw in text.splitlines():
        if not _LINE.match(raw):
            continue
        fields = raw.split("\t")
        if len(fields) < 3:
            continue
        try:
            addr = int(fields[0].strip().rstrip(":"), 16)
        except ValueError:
            continue
        text_field = fields[2].strip()
        if not text_field or text_field.startswith("("):  # (bad) u otros
            continue
        parts = text_field.split(None, 1)
        mnemonic = parts[0]
        operands = parts[1] if len(parts) > 1 else ""
        insns.append(Insn(addr, mnemonic, operands))
    return insns


def _mem_operand(operands: str) -> str:
    """El operando de memoria de una instrucción x87 (-0x10(%ebp))."""
    m = re.search(r"(-?0x[0-9a-f]+|\-?\d+)?\(%\w+\)", operands)
    return m.group(0) if m else ""


def find_ftol_body(insns, va: int = FTOL_VA):
    """Analiza el cuerpo de `__ftol` que empieza en `va` (hasta el ret)."""
    start = next((i for i, ins in enumerate(insns) if ins.addr == va), None)
    if start is None:
        return None
    body = []
    for ins in insns[start:]:
        body.append(ins)
        if base_mnemonic(ins.mnemonic) in ("ret", "retf", "iret"):
            break
    saved = None
    restored = False
    has_fistp = False
    for ins in body:
        m = base_mnemonic(ins.mnemonic)
        if m == "fstcw":
            saved = _mem_operand(ins.operands)
        elif m == "fldcw" and saved is not None and _mem_operand(ins.operands) == saved:
            restored = True
        elif m == "fistp":
            has_fistp = True
    return {
        "va": va,
        "instrs": len(body),
        "has_fistp": has_fistp,
        "control_word_net_zero": bool(saved) and restored,
    }


def find_call_sites(insns, target: int = FTOL_VA):
    return [i for i, ins in enumerate(insns)
            if ins.mnemonic == "call" and ins.operands.strip() == f"0x{target:x}"]


def _reader_consumption(insns, reader_index: int):
    """Qué bits del status word consume el lector (FNSTSW %ax → AX).

    - `sahf` mete AH en los flags: SF←C3, ZF←C0, PF←C2 (no IE).
    - `test/and al,imm`: AL = bits 0..7 → el bit 0 es IE.
    - `test/and ah,imm`: AH = bits 8..15 → 0x1=C0, 0x4=C2, 0x40=C3.
    Un lector a memoria no se puede clasificar: se marcan ambos como observables.
    """
    reader = insns[reader_index]
    operand = reader.operands.strip().lower()
    to_memory = "(" in operand or (operand and not operand.startswith("%"))
    if to_memory:
        return {"cc_checked": True, "ie_checked": True, "reader_to_memory": True}
    cc_checked = False
    ie_checked = False
    for ins in insns[reader_index + 1: reader_index + 7]:
        m = base_mnemonic(ins.mnemonic)
        if m in ("ret", "retf", "iret"):
            break
        if m == "sahf":
            cc_checked = True
            continue
        if m in ("test", "and", "or"):
            parts = ins.operands.lower().split(",")
            if len(parts) != 2:
                continue
            # AT&T: el inmediato es el operando fuente (primero), el registro el destino.
            imm, reg = parts[0].strip(), parts[1].strip()
            try:
                value = int(imm.lstrip("$"), 0)
            except ValueError:
                continue
            if reg == "%al":
                ie_checked = ie_checked or bool(value & 0x1)
            elif reg == "%ah":
                cc_checked = cc_checked or bool(value & 0x45)
    return {"cc_checked": cc_checked, "ie_checked": ie_checked, "reader_to_memory": False}


def _site_observability(insns, index: int):
    """Recorre la función desde la llamada: ¿qué puede leer el status word?"""
    cc_observable = True
    ie_observable = True
    reader = None
    reader_index = None
    for offset, ins in enumerate(insns[index + 1:], start=index + 1):
        m = base_mnemonic(ins.mnemonic)
        if m in STATUS_READERS:
            reader = ins
            reader_index = offset
            break
        if m in CONDITION_WRITERS:
            cc_observable = False
        elif m in STICKY_CLEARERS:
            ie_observable = False
        elif m in ("ret", "retf", "iret"):
            break  # el lector tiene que estar en la misma función
    result = {
        "condition_codes_observable": cc_observable and reader is not None,
        "sticky_ie_observable": ie_observable and reader is not None,
        "reader_addr": reader.addr if reader else None,
        "reader_mnemonic": reader.mnemonic if reader else None,
        "cc_checked": False,
        "ie_checked": False,
    }
    if reader_index is not None:
        consumption = _reader_consumption(insns, reader_index)
        result["cc_checked"] = consumption["cc_checked"]
        result["ie_checked"] = consumption["ie_checked"]
        if consumption["reader_to_memory"]:
            result["ie_checked"] = True
            result["cc_checked"] = True
    return result


def audit(insns, va: int = FTOL_VA):
    body = find_ftol_body(insns, va)
    call_indices = find_call_sites(insns, va)
    readers = [ins for ins in insns if base_mnemonic(ins.mnemonic) in STATUS_READERS]
    sites = []
    for index in call_indices:
        site = {"call_addr": insns[index].addr}
        site.update(_site_observability(insns, index))
        sites.append(site)
    return {
        "ftol_body": body,
        "status_readers": len(readers),
        "reader_addresses": [ins.addr for ins in readers],
        "call_sites": sites,
        "cc_observable": sum(1 for s in sites if s["condition_codes_observable"]),
        "ie_observable": sum(1 for s in sites if s["sticky_ie_observable"]),
        "cc_checked": sum(1 for s in sites if s["cc_checked"]),
        "ie_checked": sum(1 for s in sites if s["ie_checked"]),
    }


def disassemble(exe: Path) -> str:
    return subprocess.run(["objdump", "-d", str(exe)], check=True,
                          text=True, capture_output=True).stdout


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", default=str(DEFAULT_EXE))
    ap.add_argument("--asm", help="desensamblado ya generado (evita objdump)")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)

    if args.asm:
        text = Path(args.asm).read_text(errors="replace")
    else:
        exe = Path(args.exe)
        if not exe.exists():
            print(f"ftol-status: no existe el exe {exe}", file=sys.stderr)
            return 2
        text = disassemble(exe)

    insns = parse_disasm(text)
    if not insns:
        print("ftol-status: el desensamblado no tiene instrucciones", file=sys.stderr)
        return 2
    report = audit(insns)
    if report["ftol_body"] is None:
        print("ftol-status: no se encontró el cuerpo de __ftol", file=sys.stderr)
        return 2

    body = report["ftol_body"]
    sites = report["call_sites"]
    print(f"ftol-status: readers={report['status_readers']} call_sites={len(sites)} "
          f"cc_observable={report['cc_observable']} ie_observable={report['ie_observable']} "
          f"cc_checked={report['cc_checked']} ie_checked={report['ie_checked']} "
          f"cw_net_zero={int(body['control_word_net_zero'])} "
          f"ftol_instrs={body['instrs']}")
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        for site in sites:
            print(f"  call 0x{site['call_addr']:08x} reader="
                  f"{'0x%08x' % site['reader_addr'] if site['reader_addr'] else '-':>10} "
                  f"cc={int(site['condition_codes_observable'])} "
                  f"ie={int(site['sticky_ie_observable'])} "
                  f"cc_chk={int(site['cc_checked'])} ie_chk={int(site['ie_checked'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
