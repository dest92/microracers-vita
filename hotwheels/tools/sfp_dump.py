#!/usr/bin/env python3
"""Volcado del árbol de chunks de un .SFP de Hot Wheels Micro Racers (HW-008).
Formato [estático, FUN_0043b670]: chunks {u32 size (incluye el header de 8 B), u32 id}.
Contenedores: 1 (raíz), 2 (texturas), 4 (geometría/objetos). Resto = hojas."""
import struct, sys
CONTAINERS = {1, 2, 4}
NAMES = {1: 'root', 2: 'textures', 3: 'texpage', 4: 'objects', 5: 'obj5', 6: 'rec30',
         7: 'obj7', 8: 'rec2c', 9: 'obj9', 10: 'rec28', 11: 'physics', 12: 'obj12',
         13: 'rec1c', 14: 'tex14', 15: 'rec0c'}
RECSZ = {6: 0x30, 8: 0x2c, 10: 0x28, 13: 0x1c, 15: 0xc}

def walk(d, off, end, depth, out):
    while off + 8 <= end:
        size, cid = struct.unpack_from('<II', d, off)
        if size < 8 or off + size > end:
            out.append('%s!! chunk inválido @0x%x size=0x%x id=%d' % ('  ' * depth, off, size, cid))
            return False
        extra = ''
        if cid in RECSZ:
            extra = ' n=%d' % ((size - 8) // RECSZ[cid])
        if cid == 3:
            extra = ' u32=0x%x' % struct.unpack_from('<I', d, off + 8)[0]
        out.append('%s@0x%07x id=%-2d %-8s size=0x%x%s' % ('  ' * depth, off, cid, NAMES.get(cid, '?'), size, extra))
        if cid in CONTAINERS and not walk(d, off + 8, off + size, depth + 1, out):
            return False
        off += size
    return off == end

def main(path):
    d = open(path, 'rb').read()
    out = []
    ok = walk(d, 0, len(d), 0, out)
    print('\n'.join(out))
    print('OK' if ok else 'ERROR', len(d))
    return 0 if ok else 1

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
