#!/usr/bin/env python3
"""gen_import_argc.py — tabla dll!func -> bytes de argumentos stdcall.

Fuente: decoración `__imp__Nombre@N` de las import libs i686 de llvm-mingw
(shared/opt/llvm-mingw-*/i686-w64-mingw32/lib/lib<dll>.a). N = bytes que el
callee limpia (stdcall). Sin `@N` (p.ej. wsprintfA) = cdecl: el callee no limpia.
El stub-logger del integrador usa esto para volver de un import faltante SIN
correr la pila.

Uso: gen_import_argc.py <exe> <salida.inc>
"""
import glob, os, re, struct, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

def imports(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24
    secs = []
    for i in range(nsec):
        s = opt + optsz + i * 40
        vs, va, rs, rp = struct.unpack_from("<IIII", d, s + 8)
        secs.append((va, max(vs, rs), rp))
    def off(rva):
        for va, sz, rp in secs:
            if va <= rva < va + sz:
                return rva - va + rp
        raise ValueError(hex(rva))
    def cstr(o):
        return d[o:d.index(b"\0", o)].decode("latin1")
    imp_rva = struct.unpack_from("<I", d, opt + 96 + 8)[0]
    out = []
    o = off(imp_rva)
    while True:
        oft, _, _, name, ft = struct.unpack_from("<IIIII", d, o)
        if name == 0:
            break
        dll = cstr(off(name))
        t = off(oft or ft)
        while True:
            v = struct.unpack_from("<I", d, t)[0]
            if v == 0:
                break
            if not v & 0x80000000:
                out.append((dll, cstr(off(v) + 2)))
            t += 4
        o += 20
    return out

def decorations(dll):
    mingw = glob.glob(os.path.join(ROOT, "shared/opt/llvm-mingw-*"))[0]
    lib = os.path.join(mingw, "i686-w64-mingw32/lib", "lib" + dll.lower().replace(".dll", "") + ".a")
    if not os.path.exists(lib):
        return {}
    nm = subprocess.run([os.path.join(mingw, "bin/llvm-nm"), lib], capture_output=True, text=True).stdout
    dec = {}
    for m in re.finditer(r"__imp__([A-Za-z0-9_]+)(?:@(\d+))?$", nm, re.M):
        dec.setdefault(m.group(1), int(m.group(2)) if m.group(2) else -1)
    return dec

def main():
    exe, out = sys.argv[1], sys.argv[2]
    rows, cache = [], {}
    for dll, fn in imports(exe):
        dec = cache.setdefault(dll, decorations(dll))
        n = dec.get(fn)
        rows.append((dll, fn, n))
    with open(out, "w") as f:
        f.write("// Generado por tools/gen_import_argc.py — NO editar.\n")
        f.write("// {dll, func, bytes stdcall (-1 = cdecl/sin limpieza, -2 = desconocido)}\n")
        for dll, fn, n in rows:
            f.write('{"%s", "%s", %d},\n' % (dll, fn, -2 if n is None else n))
    unk = [r for r in rows if r[2] is None]
    print("%d imports, %d sin decoración conocida%s" % (len(rows), len(unk),
          (": " + ", ".join(d + "!" + f for d, f, _ in unk)) if unk else ""))

main()
