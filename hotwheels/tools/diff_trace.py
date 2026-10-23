#!/usr/bin/env python3
"""Oráculo diferencial: compara la traza de llamadas Win32 de impl (HW-007 bajo qemu-arm/Vita)
contra la traza de referencia de Wine (docs/wine-ref/boot-trace.txt) y marca la PRIMERA divergencia.

  tools/diff_trace.py [--ref docs/wine-ref/boot-trace.txt] IMPL.log [--strict] [--context 5]
  tools/diff_trace.py --ref A.txt B.txt           # compara dos trazas cualesquiera
Salida: 0 = impl coincide con la referencia (hasta donde llegó); 1 = divergencia; 2 = error de uso.
Si impl se corta antes de terminar la referencia (no diverge) informa "impl llegó hasta la línea N de la ref".

Formato aceptado (impl y ref; ver coordination/wine.md):
  [prefijo opcional] [seq opcional] [Dll.]Func(arg,arg,...) [= ret] [@caller] [xN]
  - arg: hex (con o sin 0x), `P` (puntero cualquiera) o "string" (se compara el texto, sin distinguir
    mayúsculas ni / vs \\).  Un puntero real seguido de "texto" también vale (formato relay de Wine).
  - Grupos colapsados de la ref: `Func(...) = r xN` y `(a ; b ; c) xN` significan "1 o más repeticiones".
  - Líneas que no parecen una llamada a una API conocida se ignoran (el log de impl puede tener ruido).

Qué se compara (por defecto): nombre de la función (sin DLL, sin mayúsculas), argumentos string, y la
CLASE del retorno (0 / -1 / otro). Con --strict: también todos los args escalares no-P y el retorno exacto
cuando no es puntero/volátil.  Ignorados siempre (dependen del allocator/polling): HeapCreate/Alloc/Free/
ReAlloc, VirtualAlloc/Free, timeGetTime, PeekMessageA, TranslateMessage, DispatchMessageA, DefWindowProcA.
"""
import argparse
import os
import re
import sys

IGNORE = {n.lower() for n in (
    'HeapCreate', 'HeapAlloc', 'HeapFree', 'HeapReAlloc', 'HeapDestroy', 'GetProcessHeap',
    'VirtualAlloc', 'VirtualFree', 'timeGetTime', 'PeekMessageA', 'TranslateMessage',
    'DispatchMessageA', 'DefWindowProcA', 'GetTickCount', 'QueryPerformanceCounter')}

CALL_RE = re.compile(
    r'^\s*(?:\[[^\]]*\]\s*)*(?:\d+\s+)?(?P<fn>(?:[A-Za-z0-9_]+\.)?[A-Za-z_][A-Za-z0-9_@]*)\((?P<body>.*)$')


def split_top(s):
    """parte `a,b,"x,y",c) = r @c xN` -> (args[], tail)"""
    args, cur, i, q = [], '', 0, False
    while i < len(s):
        c = s[i]
        if q:
            cur += c
            if c == '\\' and i + 1 < len(s):
                cur += s[i + 1]
                i += 1
            elif c == '"':
                q = False
        else:
            if c == '"':
                q = True
                cur += c
            elif c == ',':
                args.append(cur.strip())
                cur = ''
            elif c == ')':
                if cur.strip() or args:
                    args.append(cur.strip())
                return args, s[i + 1:]
            else:
                cur += c
        i += 1
    return None, ''  # sin paréntesis de cierre


def norm_str(t):
    t = t.strip()
    if t.startswith('L'):
        t = t[1:]
    t = t[1:-1] if len(t) >= 2 and t[0] == '"' and t[-1] == '"' else t
    t = t.replace('\\\\', '/').replace('\\', '/').lower().rstrip('~')
    return t


def parse_arg(a):
    a = a.strip()
    m = re.match(r'^(?:0x)?([0-9a-fA-F]+)\s*(L?".*)$', a)  # puntero + string (relay)
    if m:
        a = m.group(2)
    if a.startswith('"') or a.startswith('L"'):
        return ('s', norm_str(a))
    if a == 'P':
        return ('p', None)
    if a == '?':
        return ('p', None)
    try:
        return ('i', int(a, 16))
    except ValueError:
        return ('p', None)


def ret_class(r):
    if r in (None, '', '?', 'P', 'V'):
        return None
    try:
        v = int(r, 16)
    except ValueError:
        return None
    if v == 0:
        return '0'
    if v == 0xffffffff:
        return '-1'
    return 'nz'


def parse_call(text):
    """-> dict(fn, args[(k,v)], ret(str|None), rep(bool)) o None"""
    # Formato del wrapper host: [hw] trace DLL!Func(arg0 arg1"str") -> ret.
    # Sus args son palabras separadas por espacios; conservar los espacios dentro de strings.
    tm = re.match(r'^\s*\[hw\]\s+trace\s+(?P<fn>[A-Za-z0-9_.]+)!(?P<name>[A-Za-z_][A-Za-z0-9_@]*)\((?P<body>.*)\)\s+->\s+(?P<ret>\S+)\s*$', text)
    if tm:
        body = tm.group('body')
        parts, cur, quoted, i = [], '', False, 0
        while i < len(body):
            ch = body[i]
            if ch == '"':
                quoted = not quoted
                cur += ch
            elif ch.isspace() and not quoted:
                if cur:
                    parts.append(cur); cur = ''
            else:
                cur += ch
            i += 1
        if cur:
            parts.append(cur)
        normalized = '%s(%s) = %s' % (tm.group('name'), ','.join(parts), tm.group('ret'))
        return parse_call(normalized)
    m = CALL_RE.match(text)
    if not m:
        return None
    fn = m.group('fn')
    args, tail = split_top(m.group('body'))
    if args is None:
        return None
    name = fn.split('.')[-1].lower()
    ret = None
    mr = re.match(r'\s*=\s*(\S+)', tail)
    if mr:
        ret = mr.group(1).lower().replace('0x', '')
    rep = bool(re.search(r'\bx\d+\s*$', tail))
    return {'fn': name, 'raw': text.strip(), 'args': [parse_arg(a) for a in args], 'ret': ret, 'rep': rep}


def load_ref(path):
    """-> lista de tokens: ('call', call) | ('loop', [calls]) con '1 o más' repeticiones"""
    toks = []
    for ln, line in enumerate(open(path, errors='replace'), 1):
        line = line.rstrip('\n')
        if not line or line.startswith('#'):
            continue
        line = re.sub(r'^\s*\d+\s+', '', line)  # seq
        if line.startswith('(') and re.search(r'\)\s*x\d+\s*$', line):
            body = re.sub(r'\)\s*x\d+\s*$', '', line[1:])
            calls = [parse_call(p.strip()) for p in body.split(' ; ')]
            calls = [c for c in calls if c]
            toks.append(('loop', calls, ln))
            continue
        c = parse_call(line)
        if c:
            toks.append(('loop', [c], ln) if c['rep'] else ('call', [c], ln))
    return toks


def load_impl(path, known):
    out = []
    for ln, line in enumerate(open(path, errors='replace'), 1):
        line = line.rstrip('\n')
        g = re.sub(r'^\s*\d+\s+', '', line)
        if g.startswith('(') and re.search(r'\)\s*x\d+\s*$', g):  # grupo colapsado (traza ref) -> 1 vuelta
            cs = [parse_call(x.strip()) for x in re.sub(r'\)\s*x\d+\s*$', '', g[1:]).split(' ; ')]
        else:
            cs = [parse_call(line)]
        for c in cs:
            if c and c['fn'] in known:
                c['line'] = ln
                out.append(c)
    return out


def match(ref, got, strict):
    """None si coinciden, o string con el motivo"""
    if ref['fn'] != got['fn']:
        return 'función distinta'
    ra, ga = ref['args'], got['args']
    for i, (r, g) in enumerate(zip(ra, ga)):
        if r[0] == 's' or g[0] == 's':
            if r != g:
                return 'arg%d string: ref=%r impl=%r' % (i, r[1], g[1])
        elif strict and r[0] == 'i' and g[0] == 'i' and r[1] != g[1]:
            return 'arg%d: ref=%x impl=%x' % (i, r[1], g[1])
    if strict and len(ra) != len(ga):
        return 'nº de args: ref=%d impl=%d' % (len(ra), len(ga))
    rc, gc = ret_class(ref['ret']), ret_class(got['ret'])
    if rc is not None and gc is not None and rc != gc:
        return 'retorno: ref=%s impl=%s' % (ref['ret'], got['ret'])
    if strict and ref['ret'] and got['ret'] and ret_class(ref['ret']) is not None and ref['ret'] != got['ret'] \
            and ref['ret'] not in ('p', 'v'):
        return 'retorno exacto: ref=%s impl=%s' % (ref['ret'], got['ret'])
    return None


def compare(toks, impl, strict=False):
    """Devuelve dict: ok, matched, ref_line, ref_text, impl_line, impl_text, reason, ref_done"""
    impl = [c for c in impl if c['fn'] not in IGNORE]
    ti = 0       # índice en toks
    ii = 0       # índice en impl
    matched = 0
    last_ref_line = 0

    def expect_first(k):
        # primera llamada "esperable" (no ignorada) a partir del token k
        while k < len(toks):
            kind, calls, ln = toks[k]
            calls = [c for c in calls if c['fn'] not in IGNORE]
            if calls:
                return k, calls, ln
            k += 1
        return None, None, None

    while True:
        k, calls, ln = expect_first(ti)
        if k is None:  # referencia agotada
            return {'ok': True, 'matched': matched, 'ref_done': True, 'impl_extra': len(impl) - ii}
        ti = k
        kind = toks[k][0]
        if ii >= len(impl):
            return {'ok': True, 'matched': matched, 'ref_done': False, 'ref_line': ln, 'ref_text': calls[0]['raw'],
                    'impl_len': len(impl)}
        if kind == 'call':
            why = match(calls[0], impl[ii], strict)
            if why:
                return {'ok': False, 'matched': matched, 'ref_line': ln, 'ref_text': calls[0]['raw'],
                        'impl_line': impl[ii].get('line'), 'impl_text': impl[ii]['raw'], 'reason': why, 'ii': ii}
            matched += 1
            ii += 1
            ti += 1
        else:  # loop: consumir >=1 vueltas completas mientras coincidan; luego seguir
            per = len(calls)
            iters = 0
            while ii + per <= len(impl) and all(
                    not match(calls[j], impl[ii + j], strict if iters == 0 else True) for j in range(per)):
                # la 1.ª vuelta usa el criterio normal; las siguientes son estrictas (args escalares idénticos)
                # para que un loop colapsado no se "coma" una llamada parecida pero distinta que sigue.
                ii += per
                matched += per
                iters += 1
            if iters == 0:
                why = match(calls[0], impl[ii], strict) or 'el cuerpo del loop no coincide completo'
                return {'ok': False, 'matched': matched, 'ref_line': ln, 'ref_text': ' ; '.join(c['raw'] for c in calls),
                        'impl_line': impl[ii].get('line'), 'impl_text': impl[ii]['raw'], 'reason': why, 'ii': ii}
            ti += 1


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('impl')
    ap.add_argument('--ref', default=os.path.join(here, '..', 'docs', 'wine-ref', 'boot-trace.txt'))
    ap.add_argument('--strict', action='store_true')
    ap.add_argument('--context', type=int, default=5)
    a = ap.parse_args(argv)
    for p in (a.ref, a.impl):
        if not os.path.exists(p):
            print('no existe: %s' % p, file=sys.stderr)
            return 2
    toks = load_ref(a.ref)
    known = {c['fn'] for _, calls, _ in toks for c in calls}
    impl = load_impl(a.impl, known)
    if not impl:
        print('el log de impl no contiene llamadas a APIs conocidas (¿formato? ver coordination/wine.md)', file=sys.stderr)
        return 2
    r = compare(toks, impl, a.strict)
    if r['ok']:
        if r['ref_done']:
            print('OK: impl coincide con toda la referencia (%d llamadas comparadas; %d extra de impl al final)'
                  % (r['matched'], r.get('impl_extra', 0)))
        else:
            print('OK parcial: impl no diverge pero se cortó tras %d llamadas; la ref sigue en la línea %d: %s'
                  % (r['matched'], r['ref_line'], r['ref_text']))
        return 0
    print('DIVERGENCIA tras %d llamadas coincidentes' % r['matched'])
    print('  motivo   : %s' % r['reason'])
    print('  ref  (L%s): %s' % (r['ref_line'], r['ref_text']))
    print('  impl (L%s): %s' % (r['impl_line'], r['impl_text']))
    ii = r['ii']
    ctx = [c for c in impl[max(0, ii - a.context):ii]]
    if ctx:
        print('  contexto previo (impl, ya coincidente):')
        for c in ctx:
            print('    L%s %s' % (c.get('line'), c['raw']))
    return 1


if __name__ == '__main__':
    sys.exit(main())
