#!/usr/bin/env python3
"""tools/aot_x86.py — traductor AOT x86 (subconjunto) → C para HLE exacto (HW-052).

Toma el desensamblado AT&T de llvm-objdump de UNA función guest y genera una
función C que reproduce sus efectos observables con la semántica de box86
(x87 en double IEEE, sin redondeo intermedio salvo en stores de float):
registros x86, flags (perezosos, sólo donde un jcc los consume), memoria guest
(incluida la pila: push/pop escriben la pila real) y la pila x87.

Llamadas conocidas se reemplazan por equivalentes C exactos:
  __ftol (0x4521c4)  → trunc + saturación (FISTP m64), pop, EDX:EAX
  0x450a40 / 0x450af3 / 0x450c19 → src/hle/span_ft3.{c,h}

uso: aot_x86.py <dis> <nombre_c> > salida.c
Instrucción desconocida → error (nunca se genera código aproximado).
"""
import re
import sys

REGS32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']
REG8 = {'al': ('eax', 0), 'cl': ('ecx', 0), 'dl': ('edx', 0), 'bl': ('ebx', 0),
        'ah': ('eax', 8), 'ch': ('ecx', 8), 'dh': ('edx', 8), 'bh': ('ebx', 8)}
INSN = re.compile(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$')
CALLS = {0x4521c4: 'ftol', 0x450a40: 'span_setup', 0x450af3: 'span_ft3', 0x450c19: 'span_semi'}


def parse(lines):
    out = []
    for l in lines:
        m = INSN.match(l)
        if not m:
            continue
        a, mn, ops = int(m.group(1), 16), m.group(2), m.group(3).split('<')[0].split('#')[0].strip()
        out.append((a, mn, ops))
    return out


def split_ops(ops):
    res, depth, cur = [], 0, ''
    for ch in ops:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            res.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip():
        res.append(cur.strip())
    return res


def mem_addr(op):
    m = re.fullmatch(r'(-?0x[0-9a-f]+|-?\d+)?(?:\((%\w+)?(?:,(%\w+)(?:,(\d))?)?\))?', op)
    if not m or (m.group(1) is None and m.group(2) is None):
        raise ValueError('mem: ' + op)
    disp = int(m.group(1), 0) if m.group(1) else 0
    parts = []
    if m.group(2):
        parts.append(m.group(2)[1:])
    if m.group(3):
        sc = int(m.group(4) or 1)
        parts.append(f'{m.group(3)[1:]}*{sc}u' if sc != 1 else m.group(3)[1:])
    parts.append(f'0x{disp & 0xffffffff:x}u')
    return '(uint32_t)(' + ' + '.join(parts) + ')'


def is_mem(op):
    return not op.startswith('%') and not op.startswith('$')


def src32(op):
    if op.startswith('$'):
        return f'0x{int(op[1:], 0) & 0xffffffff:x}u'
    if op.startswith('%'):
        r = op[1:]
        if r in REGS32:
            return r
        raise ValueError('reg: ' + op)
    return f'RD32({mem_addr(op)})'


def dst32(op, val):
    if op.startswith('%'):
        return f'{op[1:]} = {val};'
    return f'WR32({mem_addr(op)}, {val});'


def gen(insns, name):
    targets = set()
    for a, mn, ops in insns:
        if mn.startswith('j'):
            t = re.match(r'0x([0-9a-f]+)', ops)
            if t:
                targets.add(int(t.group(1), 16))
    o = [f'int {name}(uint8_t* mb, hle_aot_regs* r) {{',
         '    uint32_t eax = r->eax, ecx = r->ecx, edx = r->edx, ebx = r->ebx, esp = r->esp, ebp = r->ebp, esi = r->esi, edi = r->edi;',
         '    double st[8]; int sp = 0; uint16_t fsw = 0;',
         '    uint32_t fa = 0, fb = 0, fr = 0; int fk = FK_LOGIC;  /* flags perezosos */',
         '    (void)fa; (void)fb; (void)fr; (void)fk; (void)fsw;']
    for a, mn, ops in insns:
        if a in targets:
            o.append(f'L_{a:x}: ;')
        ops_l = split_ops(ops)
        c = None
        def flags(k, A, B, R):
            return f'fk = {k}; fa = {A}; fb = {B}; fr = {R};'
        if mn == 'movl':
            s, d = ops_l
            c = dst32(d, src32(s))
        elif mn in ('addl', 'subl', 'cmpl', 'andl', 'testl'):
            s, d = ops_l
            S, D = src32(s), src32(d)
            if mn == 'addl':
                c = f'{{ uint32_t a_ = {D}, b_ = {S}, r_ = a_ + b_; {dst32(d, "r_")} {flags("FK_ADD", "a_", "b_", "r_")} }}'
            elif mn == 'subl':
                c = f'{{ uint32_t a_ = {D}, b_ = {S}, r_ = a_ - b_; {dst32(d, "r_")} {flags("FK_SUB", "a_", "b_", "r_")} }}'
            elif mn == 'cmpl':
                c = f'{{ uint32_t a_ = {D}, b_ = {S}, r_ = a_ - b_; {flags("FK_SUB", "a_", "b_", "r_")} }}'
            elif mn == 'andl':
                c = f'{{ uint32_t r_ = {D} & {S}; {dst32(d, "r_")} {flags("FK_LOGIC", "0", "0", "r_")} }}'
            else:
                c = f'{{ uint32_t r_ = {D} & {S}; {flags("FK_LOGIC", "0", "0", "r_")} }}'
        elif mn == 'testb':
            s, d = ops_l
            reg, sh = REG8[d[1:]]
            c = f'{{ uint32_t r_ = (({reg} >> {sh}) & 0xffu) & ({src32(s)} & 0xffu); {flags("FK_LOGIC8", "0", "0", "r_")} }}'
        elif mn in ('incl', 'decl'):
            (d,) = ops_l
            op = '+' if mn == 'incl' else '-'
            k = 'FK_INC' if mn == 'incl' else 'FK_DEC'
            c = f'{{ uint32_t a_ = {src32(d)}, r_ = a_ {op} 1u; {dst32(d, "r_")} fk = {k}; fa = a_; fr = r_; }}'
        elif mn == 'imull':
            if len(ops_l) == 2:
                s, d = ops_l
                c = dst32(d, f'(uint32_t)((int32_t){src32(d)} * (int64_t)(int32_t){src32(s)})') + ' fk = FK_IMUL;'
            else:
                raise ValueError('imull forma')
        elif mn in ('sarl', 'shll'):
            if len(ops_l) == 2:
                s, d = ops_l
                n = f'({src32(s)} & 31u)'
            else:
                (d,) = ops_l; n = '1u'
            D = src32(d)
            if mn == 'sarl':
                c = f'{{ uint32_t n_ = {n}; if (n_) {{ uint32_t a_ = {D}, r_ = (uint32_t)((int32_t)a_ >> n_); {dst32(d, "r_")} fk = FK_SHIFT; fa = a_; fb = n_; fr = r_; }} }}'
            else:
                c = f'{{ uint32_t n_ = {n}; if (n_) {{ uint32_t a_ = {D}, r_ = a_ << n_; {dst32(d, "r_")} fk = FK_SHIFT; fa = a_; fb = n_; fr = r_; }} }}'
        elif mn == 'cltd':
            c = 'edx = (uint32_t)((int32_t)eax >> 31);'
        elif mn == 'leal':
            s, d = ops_l
            c = f'{d[1:]} = {mem_addr(s)};'
        elif mn == 'pushl':
            (s,) = ops_l
            c = f'{{ uint32_t v_ = {src32(s)}; esp -= 4u; WR32(esp, v_); }}'
        elif mn == 'popl':
            (d,) = ops_l
            c = f'{{ uint32_t v_ = RD32(esp); esp += 4u; {dst32(d, "v_")} }}'
        elif mn == 'retl':
            c = ('{ uint32_t ret_ = RD32(esp); esp += 4u; (void)ret_; '
                 'r->eax = eax; r->ecx = ecx; r->edx = edx; r->ebx = ebx; r->esp = esp; r->ebp = ebp; r->esi = esi; r->edi = edi; '
                 'r->fsw = fsw; r->st_depth = sp; return 1; }')
        elif mn == 'calll':
            t = int(ops.split()[0], 16)
            kind = CALLS.get(t)
            if kind is None:
                raise ValueError(f'call desconocido 0x{t:x} en 0x{a:x}')
            ret = f'0x{a + 5:x}u'  # la dirección de retorno queda escrita en la pila, como el call real
            if kind == 'ftol':
                c = (f'{{ esp -= 4u; WR32(esp, {ret}); int64_t v_ = aot_ftol(st[--sp]); '
                     'eax = (uint32_t)(uint64_t)v_; edx = (uint32_t)((uint64_t)v_ >> 32); esp += 4u; }')
            elif kind == 'span_setup':
                c = (f'{{ esp -= 4u; WR32(esp, {ret}); aot_span_setup(mb, RD32(esp + 4u), RD32(esp + 8u), &eax, &ebx); esp += 4u; }}')
            elif kind == 'span_ft3':
                c = (f'{{ esp -= 4u; WR32(esp, {ret}); hle_span_regs o_; hle_span_ft3(mb, RD32(esp + 4u), RD32(esp + 8u), RD32(esp + 12u), RD32(esp + 16u), RD32(esp + 20u), eax, &o_); '
                     'eax = o_.eax; ecx = o_.ecx; edx = o_.edx; esp += 4u; }')
            else:
                c = (f'{{ esp -= 4u; WR32(esp, {ret}); hle_span_regs o_; hle_span_ft3_semi(mb, RD32(esp + 4u), RD32(esp + 8u), RD32(esp + 12u), RD32(esp + 16u), RD32(esp + 20u), RD32(esp + 24u), &o_); '
                     'eax = o_.eax; ecx = o_.ecx; edx = o_.edx; esp += 4u; }')
        elif mn == 'jmp':
            c = f'goto L_{int(ops.split()[0], 16):x};'
        elif mn in ('je', 'jne', 'jl', 'jle', 'jg', 'jge'):
            c = f'if (aot_cond_{mn}(fk, fa, fb, fr)) goto L_{int(ops.split()[0], 16):x};'
        elif mn == 'flds':
            (s,) = ops_l
            c = f'st[sp++] = (double)RDF({mem_addr(s)});'
        elif mn == 'fildl':
            (s,) = ops_l
            c = f'st[sp++] = (double)(int32_t){src32(s)};'
        elif mn in ('fmuls', 'fsubs', 'fadds', 'fdivs', 'fdivrs'):
            (s,) = ops_l
            v = f'(double)RDF({mem_addr(s)})'
            expr = {'fmuls': f'st[sp-1] * {v}', 'fsubs': f'st[sp-1] - {v}', 'fadds': f'st[sp-1] + {v}',
                    'fdivs': f'st[sp-1] / {v}', 'fdivrs': f'{v} / st[sp-1]'}[mn]
            c = f'st[sp-1] = {expr};'
        elif mn == 'fidivl':
            (s,) = ops_l
            c = f'st[sp-1] = st[sp-1] / (double)(int32_t){src32(s)};'
        elif mn in ('fsts', 'fstps'):
            (d,) = ops_l
            c = f'WRF({mem_addr(d)}, (float)st[sp-1]);' + (' --sp;' if mn == 'fstps' else '')
        elif mn == 'fsubrp':
            # AT&T "fsubrp %st,%st(1)" == opcode DE E9 == Intel FSUBP ST(1),ST(0):
            # ST(1) = ST(1) - ST(0); pop. (Los mnemónicos fsub/fsubr con destino st(i)
            # están invertidos en AT&T respecto de Intel: verificado por bytes.)
            if ops.replace(' ', '') not in ('%st,%st(1)', ''):
                raise ValueError('fsubrp forma: ' + ops)
            c = 'st[sp-2] = st[sp-2] - st[sp-1]; --sp;'
        elif mn == 'fcomps':
            (s,) = ops_l
            c = f'{{ double b_ = (double)RDF({mem_addr(s)}); fsw = aot_fcom(st[sp-1], b_, sp); --sp; }}'
        elif mn == 'fnstsw':
            (d,) = ops_l
            if d != '%ax':
                raise ValueError('fnstsw ' + d)
            c = 'eax = (eax & 0xffff0000u) | aot_fsw_top(fsw, sp);'
        else:
            raise ValueError(f'instrucción no soportada {mn} {ops} en 0x{a:x}')
        o.append(f'    /* {a:06x} {mn} {ops} */ {c}')
    o.append('    return 0; /* no alcanzado: sale por ret */')
    o.append('}')
    return '\n'.join(o)


if __name__ == '__main__':
    insns = parse(open(sys.argv[1]).read().splitlines())
    print(gen(insns, sys.argv[2]))
