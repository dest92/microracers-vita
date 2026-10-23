#!/usr/bin/env python3
"""verif_hle_capture.py — prueba de captura para HLE por inicio de bloque (HW-052).

El mecanismo dyn86_intrin captura una función sólo si TODA entrada crea un bloque
que empieza en su primer byte. Sobre el desensamblado ALINEADO a instrucciones
(llvm-objdump), cuenta respecto de [entry, end]:
  - call directo a entry            -> permitido
  - jmp/jcc directo a entry         -> PROHIBIDO si viene de AFUERA (tail call)
  - salto desde AFUERA al cuerpo    -> PROHIBIDO (entrada que no pasa por entry)
  - saltos internos                 -> irrelevantes (bloques del propio cuerpo)
  - referencia en datos a entry     -> permitida (call indirecto)
  - ret dentro del cuerpo           -> informativo (debe existir)
Diferencia con D2Vita/tools/verif_intrin_capture.py: aquel escanea bytes sin
alinear y cuenta saltos internos (falsos positivos en funciones grandes).

uso: verif_hle_capture.py <desensamblado llvm-objdump> <entry> <end_incl>
"""
import re
import sys

INSN = re.compile(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$')
TARGET = re.compile(r'0x([0-9a-f]+)')


def analyze(lines, entry, end):
    calls = ext_jmp_entry = ext_into_body = rets = 0
    bad = []
    for line in lines:
        m = INSN.match(line)
        if not m:
            continue
        src, mnem, ops = int(m.group(1), 16), m.group(2), m.group(3)
        inside = entry <= src <= end
        if inside and mnem.startswith('ret'):
            rets += 1
        if not (mnem.startswith('j') or mnem.startswith('call')) or '*' in ops or '[' in ops:
            continue
        t = TARGET.search(ops)
        if not t:
            continue
        tgt = int(t.group(1), 16)
        if not (entry <= tgt <= end) or inside:
            continue
        if tgt == entry:
            if mnem.startswith('call'):
                calls += 1
            else:
                ext_jmp_entry += 1
                bad.append(('jmp externo a la entrada', src))
        else:
            ext_into_body += 1
            bad.append((f'salto externo al cuerpo ->{tgt:#x}', src))
    return {'calls': calls, 'ext_jmp_entry': ext_jmp_entry,
            'ext_into_body': ext_into_body, 'rets': rets, 'bad': bad,
            'ok': ext_jmp_entry == 0 and ext_into_body == 0 and rets > 0}


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    lines = open(argv[1]).read().splitlines()
    entry, end = int(argv[2], 0), int(argv[3], 0)
    r = analyze(lines, entry, end)
    print(f'objetivo {entry:#010x}..{end:#010x}: call={r["calls"]} jmp_ext_entrada={r["ext_jmp_entry"]} '
          f'saltos_ext_cuerpo={r["ext_into_body"]} ret={r["rets"]}')
    for k, s in r['bad'][:10]:
        print(f'  !! {k} desde {s:#010x}')
    print('VERDICTO: CAPTURA COMPLETA' if r['ok'] else 'VERDICTO: CAPTURA INCOMPLETA — no armar')
    return 0 if r['ok'] else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
