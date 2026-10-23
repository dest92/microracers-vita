#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Werror -Isrc -I../winx86-dx/include tests/test_ddraw.cpp ../winx86-dx/src/ddraw/software.cpp src/ddraw/png.cpp -o build/test_ddraw
build/test_ddraw
c++ -std=c++17 -Wall -Wextra -Werror -I../winx86-dx/include \
    tests/test_winx86_dx_ddraw.cpp ../winx86-dx/src/ddraw/software.cpp \
    -o build/test_winx86_dx_ddraw
build/test_winx86_dx_ddraw
python3 - <<'PYPNG'
import pathlib, struct, zlib
p=pathlib.Path('build/test-ddraw.png').read_bytes()
assert p[:8]==b'\x89PNG\r\n\x1a\n'
pos=8
while pos<len(p):
    n=struct.unpack_from('>I',p,pos)[0]
    kind=p[pos+4:pos+8]
    data=p[pos+8:pos+8+n]
    if kind==b'IHDR': assert data[:8]==struct.pack('>II',2,1)
    if kind==b'IDAT':
        raw=zlib.decompress(data)
        assert raw[:5]==bytes([0,0x12,0x34,0x56,0xff]),raw[:5]
    pos+=12+n
PYPNG
