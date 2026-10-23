#!/usr/bin/env python3
"""Test diferencial HW-052: swRasterFT3/Semi traducidos AOT a C (src/hle/aot_raster.c)
contra los bytes x86 ORIGINALES ejecutados en Unicorn sobre la imagen completa del exe
(incluye __ftol, 0x450a40 y los spans originales). FPU con CW=0x27F (precisión 53 bits,
≡ double IEEE ≡ box86). Compara: imagen .data/.bss completa, framebuffer, textura,
tabla de mezcla y registros/ESP a la salida. Requiere HotWheels.exe local + unicorn
(.venv-diff); si faltan → SKIP."""
import ctypes, hashlib, os, random, struct, subprocess, sys, tempfile
from pathlib import Path

HW = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get('HW_EXE', HW / 'build-vita/stage/hotwheels/HotWheels.exe'))
MD5 = '4fbf44e9571a22801cbd6a926280eb19'
FUNCS = {'opaco': (0x41d5a0, 'aot_swRasterFT3'), 'semi': (0x420d30, 'aot_swRasterFT3Semi')}
FB, TEX, BLEND, VTX, STACK, RET = 0x02000000, 0x01000000, 0x01800000, 0x00e00000, 0x03000000, 0x00f00000
MEM = 0x04000000
FB_SIZE = 0x100000


class Regs(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint32) for n in ('eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi')] + \
               [('fsw', ctypes.c_uint16), ('st_depth', ctypes.c_int)]


def load_image(exe):
    f = exe.read_bytes()
    pe = struct.unpack_from('<I', f, 0x3c)[0]
    base = struct.unpack_from('<I', f, pe + 52)[0]
    size = struct.unpack_from('<I', f, pe + 80)[0]
    img = bytearray(size)
    img[:0x400] = f[:0x400]
    nsec = struct.unpack_from('<H', f, pe + 6)[0]
    opt = struct.unpack_from('<H', f, pe + 20)[0]
    for i in range(nsec):
        o = pe + 24 + opt + i * 40
        vsz, rva, rsz, raw = struct.unpack_from('<IIII', f, o + 8)
        img[rva:rva + min(rsz, vsz)] = f[raw:raw + min(rsz, vsz)]
    return base, bytes(img)


def build_lib(tmp):
    so = Path(tmp) / 'libaot.so'
    extra = os.environ.get('HW_AOT_EXTRA_INC')  # mutaciones: dir con hle/aot_runtime.h alternativo
    subprocess.check_call(['cc', '-O2', '-std=gnu11', '-shared', '-fPIC'] + (['-I', extra] if extra else []) + ['-I', str(HW / 'src'),
                           str(HW / 'src/hle/aot_raster.c'), str(HW / 'src/hle/span_ft3.c'), '-o', str(so)])
    lib = ctypes.CDLL(str(so))
    for _, name in FUNCS.values():
        getattr(lib, name).argtypes = [ctypes.c_void_p, ctypes.POINTER(Regs)]
    return lib


def setup_globals(mem_write, rnd, kind, base_img_size):
    w, h = rnd.choice([(320, 240), (640, 480), (800, 600)])
    pitch = w + rnd.choice([0, 0, 32])
    mem_write(0x46c0bc, struct.pack('<I', FB))
    mem_write(0x46c08c, struct.pack('<I', pitch))
    mem_write(0x46c0f4, struct.pack('<I', w))
    mem_write(0x46c200, struct.pack('<I', h))
    mem_write(0x46c0d4, struct.pack('<I', TEX))
    mem_write(0x46c170, struct.pack('<I', BLEND))
    return w, h


def vertex(rnd, w, h):
    m = rnd.random()
    if m < 0.7:   # dentro de pantalla
        x, y = rnd.uniform(0, w - 1), rnd.uniform(0, h - 1)
    else:         # con recorte
        x, y = rnd.uniform(-w * 0.5, w * 1.5), rnd.uniform(-h * 0.5, h * 1.5)
    return struct.pack('<4f', x, y, rnd.uniform(0, 255), rnd.uniform(0, 255))


def run_x86(base, img, va, init):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn import x86_const as X
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(base, len(img)); mu.mem_write(base, img)
    for a, data in init:
        if not any(a >= r0 and a < r0 + rs for r0, rs in ((base, len(img)),)):
            pass
    for r0, rs in ((FB, FB_SIZE), (TEX, 0x10000), (BLEND, 0x10000), (VTX, 0x1000), (STACK - 0x10000, 0x10000), (RET, 0x1000)):
        mu.mem_map(r0, rs)
    for a, data in init:
        mu.mem_write(a, data)
    mu.reg_write(X.UC_X86_REG_FPCW, 0x27F)
    sp = STACK - 0x200
    mu.mem_write(sp, struct.pack('<4I', RET, VTX, VTX + 16, VTX + 32))
    regs = {X.UC_X86_REG_ESP: sp, X.UC_X86_REG_EAX: 0x11, X.UC_X86_REG_ECX: 0x22, X.UC_X86_REG_EDX: 0x33,
            X.UC_X86_REG_EBX: 0x44, X.UC_X86_REG_EBP: 0x55, X.UC_X86_REG_ESI: 0x66, X.UC_X86_REG_EDI: 0x77}
    for r, v in regs.items():
        mu.reg_write(r, v)
    mu.emu_start(va, RET, count=50_000_000)
    out_regs = tuple(mu.reg_read(r) for r in (X.UC_X86_REG_EAX, X.UC_X86_REG_ECX, X.UC_X86_REG_EDX, X.UC_X86_REG_EBX,
                                               X.UC_X86_REG_ESP, X.UC_X86_REG_EBP, X.UC_X86_REG_ESI, X.UC_X86_REG_EDI))
    snap = (bytes(mu.mem_read(0x45f000, base + len(img) - 0x45f000)), bytes(mu.mem_read(FB, FB_SIZE)),
            bytes(mu.mem_read(TEX, 0x10000)), bytes(mu.mem_read(BLEND, 0x10000)))
    return out_regs, snap, sp


def run_c(lib, name, base, img, init, sp):
    mem = (ctypes.c_uint8 * MEM)()
    ctypes.memmove(ctypes.addressof(mem) + base, img, len(img))
    for a, data in init:
        ctypes.memmove(ctypes.addressof(mem) + a, data, len(data))
    ctypes.memmove(ctypes.addressof(mem) + sp, struct.pack('<4I', RET, VTX, VTX + 16, VTX + 32), 16)
    r = Regs(0x11, 0x22, 0x33, 0x44, sp, 0x55, 0x66, 0x77, 0, 0)
    ok = getattr(lib, name)(ctypes.addressof(mem), ctypes.byref(r))
    assert ok == 1 and r.st_depth == 0, (ok, r.st_depth)
    out_regs = (r.eax, r.ecx, r.edx, r.ebx, r.esp, r.ebp, r.esi, r.edi)
    snap = (bytes(mem[0x45f000:base + len(img)]), bytes(mem[FB:FB + FB_SIZE]),
            bytes(mem[TEX:TEX + 0x10000]), bytes(mem[BLEND:BLEND + 0x10000]))
    return out_regs, snap


def main():
    try:
        import unicorn  # noqa: F401
    except ImportError:
        venv = HW / '.venv-diff/bin/python'
        if venv.is_file() and os.environ.get('HW_NO_REEXEC') != '1':
            os.environ['HW_NO_REEXEC'] = '1'
            os.execv(str(venv), [str(venv), __file__])
        print('SKIP test_aot_raster: falta unicorn'); return 0
    if not EXE.is_file():
        print('SKIP test_aot_raster: falta HotWheels.exe'); return 0
    if not (HW / 'src/hle/aot_raster.c').is_file():
        print('SKIP test_aot_raster: falta src/hle/aot_raster.c (tools/gen_aot_raster.sh)'); return 0
    if hashlib.md5(EXE.read_bytes()).hexdigest() != MD5:
        print('FAIL: md5 de HotWheels.exe distinto'); return 1
    base, img = load_image(EXE)
    rnd = random.Random(int(os.environ.get('HW_SEED', '7')))
    cases = int(os.environ.get('HW_CASES', '200'))
    names = ('fb', 'tex', 'blend')
    with tempfile.TemporaryDirectory() as tmp:
        lib = build_lib(tmp)
        drawn = 0
        for i in range(cases):
            kind = 'semi' if i % 2 else 'opaco'
            va, cname = FUNCS[kind]
            init = []
            w, h = setup_globals(lambda a, d: init.append((a, d)), rnd, kind, len(img))
            init.append((VTX, vertex(rnd, w, h) + vertex(rnd, w, h) + vertex(rnd, w, h)))
            init.append((FB, rnd.randbytes(FB_SIZE)))
            init.append((TEX, rnd.randbytes(0x10000)))
            init.append((BLEND, rnd.randbytes(0x10000)))
            rx, sx, sp = run_x86(base, img, va, init)
            rc, sc = run_c(lib, cname, base, img, init, sp)
            fb0 = init[-3][1]
            drawn += sx[1] != fb0
            if rx != rc or sx != sc:
                print(f'FAIL caso {i} ({kind}) {w}x{h}: regs x86={[hex(v) for v in rx]} C={[hex(v) for v in rc]}')
                for k, (a, b) in enumerate(zip(sx, sc)):
                    if a != b:
                        d = next(j for j in range(len(a)) if a[j] != b[j])
                        lab = 'data' if k == 0 else names[k - 1]
                        where = hex(0x45f000 + d) if k == 0 else hex(d)
                        print(f'  región {lab} difiere desde +{where}: x86={a[d]:#x} C={b[d]:#x}')
                return 1
    print(f'PASS test_aot_raster: {cases} triángulos (opaco/semi, con recorte), {drawn} dibujaron píxeles; '
          f'imagen .data/.bss, framebuffer, textura, tabla y registros idénticos al x86 original')
    return 0


if __name__ == '__main__':
    sys.exit(main())
