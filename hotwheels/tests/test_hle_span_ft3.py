#!/usr/bin/env python3
"""Test diferencial HW-052: hle_span_ft3 (C nativo) vs bytes x86 ORIGINALES de
0x450af3 ejecutados en Unicorn. Requiere HotWheels.exe local (no se commitea;
HW_EXE o build-vita/stage/hotwheels/HotWheels.exe) y unicorn (.venv-diff).
Sin exe → SKIP (rc 0 con aviso)."""
import ctypes, os, random, struct, subprocess, sys, tempfile
from pathlib import Path

HW = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get('HW_EXE', HW / 'build-vita/stage/hotwheels/HotWheels.exe'))
MD5 = '4fbf44e9571a22801cbd6a926280eb19'
FN_VA, FN_END = 0x450af3, 0x450b83
SEMI_VA, SEMI_END = 0x450c19, 0x450c8a
BLEND = 0x03800000
STEP, USTEP = 0x4679c8, 0x4679cc
TEX, DST, STACK, RET = 0x01000000, 0x02000000, 0x03000000, 0x00400000
MEM = 0x04000000  # espacio guest emulado (64 MiB) para el lado C


def text_bytes(exe, va, end):
    f = exe.read_bytes()
    pe = struct.unpack_from('<I', f, 0x3c)[0]
    base = struct.unpack_from('<I', f, pe + 52)[0]
    nsec = struct.unpack_from('<H', f, pe + 6)[0]
    opt = struct.unpack_from('<H', f, pe + 20)[0]
    for i in range(nsec):
        o = pe + 24 + opt + i * 40
        vsz, rva, rsz, raw = struct.unpack_from('<IIII', f, o + 8)
        if base + rva <= va < base + rva + vsz:
            off = raw + va - (base + rva)
            return f[off:off + (end - va)]
    raise SystemExit('VA fuera de secciones')


def build_lib(tmp):
    so = Path(tmp) / 'libspan.so'
    subprocess.check_call(['cc', '-O2', '-shared', '-fPIC', '-I', str(HW / 'src'),
                           os.environ.get('HW_SPAN_SRC', str(HW / 'src/hle/span_ft3.c')), '-o', str(so)])
    lib = ctypes.CDLL(str(so))
    lib.hle_span_ft3.argtypes = [ctypes.c_void_p] + [ctypes.c_uint32] * 6 + [ctypes.c_void_p]
    lib.hle_span_ft3_semi.argtypes = [ctypes.c_void_p] + [ctypes.c_uint32] * 6 + [ctypes.c_void_p]
    return lib


def run_x86(code, args, eax, step, ustep, tex, dst_before, va=FN_VA, blend=None):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(0x450000, 0x2000); mu.mem_write(va, code)
    if blend is not None: mu.mem_map(BLEND, 0x10000); mu.mem_write(BLEND, blend)
    mu.mem_map(0x467000, 0x1000); mu.mem_write(STEP, struct.pack('<I', step)); mu.mem_write(USTEP, bytes([ustep]))
    mu.mem_map(TEX, 0x10000); mu.mem_write(TEX, tex)
    mu.mem_map(DST, 0x10000); mu.mem_write(DST, dst_before)
    mu.mem_map(STACK - 0x10000, 0x10000); mu.mem_map(RET, 0x1000)
    sp = STACK - 0x100
    mu.mem_write(sp, struct.pack('<%dI' % (len(args) + 1), RET, *args))
    mu.reg_write(UC_X86_REG_ESP, sp); mu.reg_write(UC_X86_REG_EAX, eax)
    for r, val in ((UC_X86_REG_EBX, 0x11111111), (UC_X86_REG_ESI, 0x22222222), (UC_X86_REG_EDI, 0x33333333), (UC_X86_REG_EBP, 0x44444444)):
        mu.reg_write(r, val)
    mu.emu_start(va, RET)
    assert mu.reg_read(UC_X86_REG_EBX) == 0x11111111 and mu.reg_read(UC_X86_REG_EBP) == 0x44444444
    return (bytes(mu.mem_read(DST, 0x10000)) + bytes(mu.mem_read(TEX, 0x10000)),
            (mu.reg_read(UC_X86_REG_EAX), mu.reg_read(UC_X86_REG_ECX), mu.reg_read(UC_X86_REG_EDX)))


def run_c(lib, args, eax, step, ustep, tex, dst_before, blend=None):
    mem = (ctypes.c_uint8 * MEM)()
    ctypes.memmove(ctypes.addressof(mem) + STEP, struct.pack('<I', step), 4)
    mem[USTEP] = ustep
    ctypes.memmove(ctypes.addressof(mem) + TEX, tex, 0x10000)
    ctypes.memmove(ctypes.addressof(mem) + DST, dst_before, 0x10000)
    regs = (ctypes.c_uint32 * 3)()
    if blend is None:
        lib.hle_span_ft3(ctypes.addressof(mem), *args, eax, ctypes.addressof(regs))
    else:
        ctypes.memmove(ctypes.addressof(mem) + BLEND, blend, 0x10000)
        lib.hle_span_ft3_semi(ctypes.addressof(mem), *args, ctypes.addressof(regs))
    return bytes(mem[DST:DST + 0x10000]) + bytes(mem[TEX:TEX + 0x10000]), tuple(regs)


def main():
    try:
        import unicorn  # noqa: F401
    except ImportError:
        venv = HW / '.venv-diff/bin/python'
        if venv.is_file() and os.environ.get('HW_NO_REEXEC') != '1':
            os.environ['HW_NO_REEXEC'] = '1'
            os.execv(str(venv), [str(venv), __file__])
        print('SKIP test_hle_span_ft3: falta unicorn (python3 -m venv .venv-diff && .venv-diff/bin/pip install unicorn)')
        return 0
    if not EXE.is_file():
        print('SKIP test_hle_span_ft3: falta HotWheels.exe'); return 0
    import hashlib
    if hashlib.md5(EXE.read_bytes()).hexdigest() != MD5:
        print('FAIL: md5 de HotWheels.exe distinto'); return 1
    code = text_bytes(EXE, FN_VA, FN_END)
    semi = text_bytes(EXE, SEMI_VA, SEMI_END)
    rnd = random.Random(int(os.environ.get('HW_SEED', '1234')))
    cases = int(os.environ.get('HW_CASES', '300'))
    with tempfile.TemporaryDirectory() as tmp:
        lib = build_lib(tmp)
        for i in range(cases):
            n = rnd.choice([0, 1, 2, 3, 4, 5, 7, 8, rnd.randrange(0, 1200)])
            # 1 de cada 8: destino DENTRO de la página de textura (camino exacto por solape)
            dst = (TEX + 0x4000 + rnd.randrange(0, 0x8000)) if i % 8 == 7 else DST + 0x8000 + rnd.randrange(0, 0x2000)
            args = (dst, rnd.getrandbits(32), rnd.getrandbits(32), n, TEX | rnd.getrandbits(16))
            eax, step, ustep = rnd.getrandbits(32), rnd.getrandbits(32), rnd.getrandbits(8)
            tex = rnd.randbytes(0x10000); before = rnd.randbytes(0x10000)
            if i % 2:
                blend = rnd.randbytes(0x10000)
                sargs = args + (BLEND | rnd.getrandbits(16),)
                mx, rx = run_x86(semi, sargs, eax, step, ustep, tex, before, va=SEMI_VA, blend=blend)
                mc, rc = run_c(lib, sargs, eax, step, ustep, tex, before, blend=blend)
            else:
                mx, rx = run_x86(code, args, eax, step, ustep, tex, before)
                mc, rc = run_c(lib, args, eax, step, ustep, tex, before)
            if mx != mc or rx != rc:
                d = next((k for k in range(len(mx)) if mx[k] != mc[k]), None)
                print(f'FAIL caso {i} ({"semi" if i % 2 else "opaco"}): n={n} args={[hex(a) for a in args]} eax={eax:#x} step={step:#x} ustep={ustep:#x}')
                print(f'  regs x86={[hex(r) for r in rx]} C={[hex(r) for r in rc]} primer byte distinto={d}')
                return 1
    print(f'PASS test_hle_span_ft3: {cases} casos aleatorios (mitad spanFT3, mitad Semi), memoria y EAX/ECX/EDX idénticos al x86 original')
    return 0


if __name__ == '__main__':
    sys.exit(main())
