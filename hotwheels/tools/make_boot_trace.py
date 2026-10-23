#!/usr/bin/env python3
"""Convierte un log `WINEDEBUG=+relay` (filtrado a llamadas con ret=004xxxxx..007xxxxx, es decir
hechas DESDE HotWheels.exe) en la traza de referencia normalizada.

Uso:
  WINEDEBUG=-all,+relay wine HotWheels.exe 2>&1 \
    | grep -a --line-buffered -E "^[0-9a-f]+:(Call|Ret ) .* ret=00[4-7][0-9a-f]{5}" | head -n 400000 > relay.raw
  tools/make_boot_trace.py relay.raw --out docs/wine-ref/boot-trace.txt --limit 2000

Formato de salida (una línea por llamada, ver coordination/wine.md "formato de traza"):
  <seq> <Dll>.<Func>(<arg0>,<arg1>,...) = <retval> @<caller_va>   [xN si se colapsaron N repeticiones]
args en hex de 8 dígitos; strings como "texto" justo después del puntero (se conserva sólo el texto:
el puntero se enmascara como `P`). Los punteros a heap/stack se enmascaran `P`.
"""
import argparse
import re
import sys

CALL = re.compile(r'^([0-9a-f]+):Call (\S+?)\((.*)\) ret=([0-9a-f]{8})$')
RET = re.compile(r'^([0-9a-f]+):Ret  (\S+?)\(\) retval=([0-9a-f]{8}) ret=([0-9a-f]{8})$')
ARG = re.compile(r'([0-9a-f]{8})(?: ("(?:[^"\\]|\\.)*"|L"(?:[^"\\]|\\.)*"))?')

# Wine implementa varias APIs vía ntdll; se normalizan al nombre Win32 que ve el exe.
ALIAS = {
    'ntdll.RtlAllocateHeap': 'KERNEL32.HeapAlloc',
    'ntdll.RtlFreeHeap': 'KERNEL32.HeapFree',
    'ntdll.RtlReAllocateHeap': 'KERNEL32.HeapReAlloc',
    'ntdll.NtdllDefWindowProc_A': 'user32.DefWindowProcA',
}
POINTER_MIN = 0x10000  # < 0x10000 = valor; >= 0x10000 = puntero/handle grande -> P (salvo VAs del exe/ints chicos)


NOSTR = {'KERNEL32.WideCharToMultiByte', 'KERNEL32.MultiByteToWideChar', 'KERNEL32.GetStringTypeW',
         'KERNEL32.LCMapStringW', 'KERNEL32.CompareStringW', 'KERNEL32.GetEnvironmentStringsW'}  # strings dependientes del entorno/CRT


def norm_args(argstr, fn=''):
    out = []
    pos = 0
    argstr = argstr.strip()
    while pos < len(argstr):
        m = ARG.match(argstr, pos)
        if not m:
            break
        val = int(m.group(1), 16)
        s = m.group(2)
        if s is not None and fn in NOSTR:
            out.append('P')
        elif s is not None:
            out.append(clip(s))
        elif val >= POINTER_MIN and not (0x400000 <= val < 0x500000):
            out.append('P')
        else:
            out.append('%08x' % val if val >= 0x100 else '%x' % val)
        pos = m.end()
        if pos < len(argstr) and argstr[pos] == ',':
            pos += 1
    return out


SCALAR_RET = {'KERNEL32.GetVersion'}  # retornos grandes pero NO punteros
VOLATILE_RET = {'winmm.timeGetTime', 'KERNEL32.GetTickCount', 'KERNEL32.QueryPerformanceCounter',
                'KERNEL32.GetLocalTime', 'KERNEL32.GetSystemTime', 'KERNEL32.GetStartupInfoA'}  # void: EAX residual


def norm_ret(rv, fn=''):
    v = int(rv, 16)
    if fn in VOLATILE_RET:
        return 'V'
    if fn in SCALAR_RET:
        return '%x' % v
    if v >= POINTER_MIN and not (0x400000 <= v < 0x500000):
        return 'P'
    return '%x' % v


def clip(s, n=48):
    return s if len(s) <= n else s[:n - 1] + '~"'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('raw')
    ap.add_argument('--out', required=True)
    ap.add_argument('--limit', type=int, default=2000, help='líneas (tras colapsar) a emitir')
    ap.add_argument('--tid', help='thread id (hex) a seguir; por defecto el del primer Call')
    a = ap.parse_args()

    pending = {}  # (tid) -> stack de [name, args, caller, idx]
    entries = []  # dicts
    tid0 = a.tid
    for line in open(a.raw, errors='replace'):
        line = line.rstrip('\n')
        m = CALL.match(line)
        if m:
            tid, name, args, ret = m.groups()
            if tid0 is None:
                tid0 = tid
            if tid != tid0:
                continue
            name = ALIAS.get(name, name)
            e = {'fn': name, 'args': norm_args(args, name), 'caller': ret, 'ret': '?'}
            entries.append(e)
            pending.setdefault(tid, []).append(e)
            continue
        m = RET.match(line)
        if m:
            tid, name, rv, ret = m.groups()
            if tid != tid0:
                continue
            st = pending.get(tid, [])
            # cerrar la llamada pendiente más reciente con el mismo caller
            for i in range(len(st) - 1, -1, -1):
                if st[i]['caller'] == ret:
                    st[i]['ret'] = norm_ret(rv, st[i]['fn'])
                    del st[i]
                    break

    # colapsar ciclos de período 1..4 con la misma firma (args+ret enmascarados), repetidos >=2 veces
    # (polling timeGetTime/PeekMessageA, pares SetFilePointer/ReadFile, etc.)
    def sig(e):
        return (e['fn'], tuple(e['args']), e['ret'], e['caller'])

    def txt(e):
        return '%s(%s) = %s @%s' % (e['fn'], ','.join(e['args']), e['ret'], e['caller'])

    lines = []
    i = 0
    while i < len(entries):
        best = (1, 1)  # (periodo, repeticiones)
        for per in (1, 2, 3, 4):
            if i + per > len(entries):
                break
            base = [sig(x) for x in entries[i:i + per]]
            n = 1
            while i + (n + 1) * per <= len(entries) and [sig(x) for x in entries[i + n * per:i + (n + 1) * per]] == base:
                n += 1
            if n >= 2 and n * per > best[0] * best[1]:
                best = (per, n)
        per, n = best
        if n >= 2:
            body = ' ; '.join(txt(x) for x in entries[i:i + per])
            lines.append(('(%s) x%d' % (body, n)) if per > 1 else ('%s x%d' % (body, n)))
            i += per * n
        else:
            lines.append(txt(entries[i]))
            i += 1
    with open(a.out, 'w') as f:
        f.write('# boot-trace de referencia (Wine 11.18, HotWheels.exe, relay filtrado a ret=exe). tid=%s\n' % tid0)
        f.write('# <seq> Dll.Func(args) = ret @caller [xN]   P = puntero enmascarado; strings entre comillas\n')
        for k, t in enumerate(lines[:a.limit]):
            f.write('%04d %s\n' % (k + 1, t))
    print('llamadas=%d colapsadas=%d emitidas=%d' % (len(entries), len(lines), min(len(lines), a.limit)), file=sys.stderr)


if __name__ == '__main__':
    main()
