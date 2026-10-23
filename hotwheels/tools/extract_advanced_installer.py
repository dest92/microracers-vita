#!/usr/bin/env python3
"""Extrae el MSI/CAB/INI de un SFX Advanced Installer sin ejecutarlo.

Sólo genera archivos locales con copyright; usar un directorio ignorado por Git.
"""

import argparse
import struct
from pathlib import Path


MAGIC = b"ADVINSTSFX"
XOR_BYTES = 0x200


def extract(source: Path, destination: Path) -> list[Path]:
    destination.mkdir(parents=True, exist_ok=True)
    result = []
    with source.open("rb") as src:
        src.seek(0, 2)
        total = src.tell()
        src.seek(max(0, total - 0x4000))
        tail = src.read()
        marker = tail.rfind(MAGIC)
        if marker < 0:
            raise ValueError("No se encontró ADVINSTSFX")
        marker += total - len(tail)
        footer_offset = marker - 0x40
        if footer_offset < 0 or marker + len(MAGIC) > total:
            raise ValueError("Pie del SFX fuera del archivo")
        src.seek(footer_offset)
        footer = src.read(0x4A)
        if footer[0x40:] != MAGIC:
            raise ValueError("Pie del SFX inválido")
        _, declared_footer, count, version, _, info_offset, data_offset = (
            struct.unpack_from("<7I", footer)
        )
        if declared_footer != footer_offset or version != 100 or count > 10000:
            raise ValueError("Directorio del SFX inválido")
        if not (data_offset < info_offset <= footer_offset):
            raise ValueError("Offsets del SFX inválidos")
        src.seek(info_offset)
        entries = []
        used_names = set()
        for _ in range(count):
            raw = src.read(24)
            if len(raw) != 24:
                raise ValueError("Entrada truncada")
            _, _, xor_flag, size, offset, name_length = struct.unpack("<6I", raw)
            if name_length > 1024 or src.tell() + 2 * name_length > footer_offset:
                raise ValueError("Nombre fuera de rango")
            name = src.read(2 * name_length).decode("utf-16le")
            safe_name = Path(name.replace("\\", "/")).name
            if not safe_name or safe_name in used_names:
                raise ValueError("Nombre vacío o duplicado")
            if offset < data_offset or offset + size > info_offset:
                raise ValueError("Datos fuera de rango")
            used_names.add(safe_name)
            entries.append((safe_name, offset, size, xor_flag))
        for name, offset, size, xor_flag in entries:
            target = destination / name
            src.seek(offset)
            remaining = size
            position = 0
            with target.open("wb") as out:
                while remaining:
                    chunk = src.read(min(1 << 20, remaining))
                    if not chunk:
                        raise ValueError("Datos truncados")
                    if xor_flag == 2 and position < XOR_BYTES:
                        n = min(len(chunk), XOR_BYTES - position)
                        chunk = bytes(byte ^ 0xFF for byte in chunk[:n]) + chunk[n:]
                    out.write(chunk)
                    remaining -= len(chunk)
                    position += len(chunk)
            result.append(target)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    for path in extract(args.source, args.destination):
        print(path)
