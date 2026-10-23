#!/usr/bin/env python3
"""Symbolize HW_PC_PROFILE output and summarize native runtime components."""
import argparse
import collections
import pathlib
import re
import subprocess

MAIN = re.compile(r"pcprof: scene=(\S+) total=(\d+) translated=(\d+) native=(\d+)")
GAME = re.compile(r"pcprof-game scene=(\S+) rank=(\d+) eip=(0x[0-9a-fA-F]+) name=(.*?) samples=(\d+) pct=([0-9.]+)")
HOST = re.compile(r"pcprof-host scene=(\S+) pc=(0x[0-9a-fA-F]+) samples=(\d+)")


def category(function: str, source: str) -> str:
    f, s = function.lower(), source.lower().split(":", 1)[0]
    if any(x in s or x in f for x in ("dynarec_arm_functions", "dynarec_arm_helper", "x86primop", "x87", "fist", "idiv", "div32", "memfast", "aeabi_idiv")):
        return "dynarec helper"
    if "dynarec" in s or any(x in f for x in ("dynarun", "dyn86_", "dbgetblock", "getdb", "isinHotpage".lower())):
        return "dynarec dispatch"
    if "/src/audio/" in s or "dsound" in f or "mixer" in f or "audio" in f:
        return "audio"
    if any(x in s for x in ("/src/ddraw/png.cpp", "/src/ddraw/software.cpp", "/src/d3d/", "/src/hle/", "/src/present/", "vitagl")) or "refraster" in f or "pngpresenter" in f:
        return "render/vitaGL"
    if any(x in s for x in ("/src/shims/", "/src/ddraw/", "/src/dinput/", "/src/winmm/", "/src/integration/")):
        return "shims"
    return "runtime/host/unknown"


def source_file(source: str) -> str:
    source = source.split(" (discriminator", 1)[0]
    return re.sub(r":\d+$", "", source)


def unknown_kind(function: str, source: str) -> str:
    f, s = function.lower(), source.lower()
    if any(x in f or x in s for x in ("pcprof", "hw_pcprof", "on_prof", "pc_profile")):
        return "profiler"
    if any(x in f or x in s for x in ("pthread", "futex", "mutex", "condition_variable", "gil", "sched_yield")):
        return "locks/scheduler"
    if any(x in f or x in s for x in ("memcpy", "memmove", "memset", "calloc", "malloc", "realloc", "free", "allocator")):
        return "libc/memory"
    leaf = f.rsplit("::", 1)[-1].split("(", 1)[0]
    math_symbols = {"sin", "sinf", "sinl", "cos", "cosf", "cosl", "atan", "atanf", "atanl", "atan2", "atan2f", "atan2l", "sqrt", "sqrtf", "sqrtl", "pow", "powf", "powl"}
    if leaf in math_symbols or "/libm" in s or re.match(r"__aeabi_[df](?:2|add|sub|mul|div|cmp)", leaf):
        return "libm/soft-float"
    if "d2rt::cpu::" in f or "d2rt::cpu::" in s:
        return "runtime CPU accessors"
    if any(x in f or x in s for x in ("std::", "__gnu_cxx", "ios_base", "stl_", "bits/")):
        return "C++ runtime/containers"
    if f in ("??", "") or f.startswith("0x"):
        return "unmapped native PC"
    return "other runtime"


def symbolize(elf: pathlib.Path, pcs: list[int], tool: str) -> dict[int, tuple[str, str]]:
    result: dict[int, tuple[str, str]] = {}
    for start in range(0, len(pcs), 400):
        chunk = pcs[start : start + 400]
        proc = subprocess.run(
            [tool, "-f", "-C", "-e", str(elf), *(hex(pc) for pc in chunk)],
            check=True, capture_output=True, text=True,
        )
        lines = proc.stdout.splitlines()
        for i, pc in enumerate(chunk):
            function = lines[2 * i].strip() if 2 * i < len(lines) else "??"
            source = lines[2 * i + 1].strip() if 2 * i + 1 < len(lines) else "??:0"
            result[pc] = (function, source)
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=pathlib.Path)
    ap.add_argument("elf", type=pathlib.Path)
    ap.add_argument("--addr2line", default="addr2line")
    args = ap.parse_args()
    scenes = {}
    for line in args.log.read_text(errors="replace").splitlines():
        m = MAIN.search(line)
        if m:
            scene = m.group(1)
            scenes[scene] = {"total": int(m.group(2)), "translated": int(m.group(3)), "native": int(m.group(4)),
                             "game": [], "host": []}
            continue
        m = GAME.search(line)
        if m and m.group(1) in scenes:
            scenes[m.group(1)]["game"].append((int(m.group(2)), int(m.group(3), 16), m.group(4), int(m.group(5)), float(m.group(6))))
            continue
        m = HOST.search(line)
        if m and m.group(1) in scenes:
            scenes[m.group(1)]["host"].append((int(m.group(2), 16), int(m.group(3))))
    if not scenes:
        raise SystemExit("no pcprof scene records found")
    for name, data in scenes.items():
        resolved = symbolize(args.elf, [pc for pc, _ in data["host"]], args.addr2line)
        groups = collections.defaultdict(collections.Counter)
        unknown_groups = collections.Counter()
        for pc, count in data["host"]:
            function, source = resolved[pc]
            groups[category(function, source)][(function, source_file(source))] += count
            if category(function, source) == "runtime/host/unknown":
                unknown_groups[unknown_kind(function, source)] += count
        total = data["total"]
        print(f"## {name}: {total} muestras; PC host→dynablock→EIP guest")
        print("\n| Componente | Muestras | % total |")
        print("|---|---:|---:|")
        print(f"| Código traducido del juego | {data['translated']} | {100*data['translated']/total:.2f} |")
        for comp in ("dynarec helper", "dynarec dispatch", "shims", "render/vitaGL", "audio", "runtime/host/unknown"):
            count = sum(groups[comp].values())
            print(f"| {comp} | {count} | {100*count/total:.2f} |")
        unknown_total = sum(unknown_groups.values())
        if unknown_total:
            print("\n### runtime/host/unknown — desglose heurístico")
            print("\nClasificación aproximada por nombre del símbolo/fuente; revisar coincidencias individuales.")
            print("\n| Subcomponente | Muestras | % total | % unknown |")
            print("|---|---:|---:|---:|")
            for kind, count in unknown_groups.most_common():
                print(f"| {kind} | {count} | {100*count/total:.2f} | {100*count/unknown_total:.1f} |")
        print("\n### Guest traducido — top 30")
        print("\n| # | EIP guest | Función Ghidra | Muestras | % total |")
        print("|---:|---:|---|---:|---:|")
        for rank, eip, func, count, pct in data["game"]:
            print(f"| {rank} | `0x{eip:08x}` | `{func}` | {count} | {pct:.2f} |")
        for comp in ("dynarec helper", "dynarec dispatch", "shims", "render/vitaGL", "audio", "runtime/host/unknown"):
            rows = groups[comp].most_common(12)
            print(f"\n### {comp} — funciones host dominantes")
            print("\n| Función host | Fuente | Muestras | % total |")
            print("|---|---|---:|---:|")
            for (func, source), count in rows:
                print(f"| `{func}` | `{source}` | {count} | {100*count/total:.2f} |")
        print("\n> vitaGL es Vita-only y no ejecuta en qemu-arm; cero muestras en ese componente no valida Vita.")


if __name__ == "__main__":
    raise SystemExit(main())
