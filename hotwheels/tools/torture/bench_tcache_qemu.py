#!/usr/bin/env python3
"""Measure HW-059 cold/warm real-game translation cache pairs under qemu-arm."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import tempfile
import time

FRAME = re.compile(r"\[hw\] D3D frame=(\d+)")
TCACHE = re.compile(r"\[tcache\] stats hits=(\d+) misses=(\d+) saved=(\d+) rejected=(\d+) read_bytes=(\d+) written_bytes=(\d+)")

def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def pe_layout(path):
    data = Path(path).read_bytes()
    pe = int.from_bytes(data[0x3c:0x40], "little")
    opt = pe + 24
    base = int.from_bytes(data[opt + 28:opt + 32], "little")
    size = int.from_bytes(data[opt + 56:opt + 60], "little")
    return base, size

def execute(binary, exe, croot, droot, seconds, cache_dir, identity, guest_base, guest_size, label):
    temp_root = Path(os.environ.get("HW059_TMPDIR", "/tmp"))
    temp_root.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix=f"hw059-{label}-", dir=temp_root))
    env = os.environ.copy()
    env.update({
        "WX86_TCACHE": "1", "WX86_TCACHE_DIR": str(cache_dir), "WX86_TCACHE_ID": identity,
        "WX86_TCACHE_FIXED_IMAGE": "1", "WX86_TCACHE_TARGET": "qemu-arm-static",
        "WX86_CHAIN": "0", "WX86_TCACHE_CHAIN": "0",
        "WX86_TCACHE_GUEST_BASE": hex(guest_base), "WX86_TCACHE_GUEST_SIZE": hex(guest_size),
    })
    command = ["qemu-arm", str(binary), str(exe), str(croot), str(droot), str(run_dir), str(seconds)]
    start = time.perf_counter()
    first = None
    stdout_path = run_dir / "stdout.txt"
    with stdout_path.open("w") as log:
        proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, bufsize=1, env=env)
        def drain():
            nonlocal first
            for line in proc.stdout:
                log.write(line)
                log.flush()
                if first is None and FRAME.search(line):
                    first = time.perf_counter() - start
        reader = threading.Thread(target=drain, daemon=True)
        reader.start()
        try:
            while proc.poll() is None:
                if first is None and list(run_dir.glob("frame_recent_*.png")):
                    first = time.perf_counter() - start
                if time.perf_counter() - start > seconds + 30:
                    proc.kill()
                    raise TimeoutError("qemu-arm run exceeded timeout")
                time.sleep(0.002)
            rc = proc.wait()
            reader.join()
        except Exception:
            proc.kill()
            proc.wait()
            reader.join()
            raise
    elapsed = time.perf_counter() - start
    runtime_log = (run_dir / "run.log").read_text(errors="replace") if (run_dir / "run.log").exists() else ""
    output = stdout_path.read_text(errors="replace")
    stats = TCACHE.findall(output)
    verdict = subprocess.run(["bash", "hotwheels/tools/smoke_qemu_verdict.sh", str(run_dir)],
                             text=True, capture_output=True)
    subprocess.run([sys.executable, "hotwheels/tools/finalize_frames.py", str(run_dir)], check=True)
    frames = sorted(run_dir.glob("frame_*.png"))
    first_hash = hashlib.sha256(frames[0].read_bytes()).hexdigest() if frames else ""
    result = {
        "label": label, "rc": rc, "elapsed_s": round(elapsed, 3),
        "first_frame_s": None if first is None else round(first, 3),
        "frames": len(frames), "first_frame_hash": first_hash,
        "verdict": verdict.stdout.splitlines()[:2],
        "tcache": [tuple(map(int, row)) for row in stats[-1:]],
    }
    if rc != 0 or verdict.returncode != 0 or not frames or not stats:
        print(f"FAIL artifacts={run_dir}", file=sys.stderr)
        print(json.dumps(result, sort_keys=True), file=sys.stderr)
        raise SystemExit(1)
    shutil.rmtree(run_dir)
    return result

def main():
    if len(sys.argv) != 7:
        raise SystemExit("usage: bench_tcache_qemu.py <binary> <exe> <C-root> <D-root> <cache-dir> <secs>")
    binary, exe, croot, droot = map(Path, sys.argv[1:5])
    cache_dir = Path(sys.argv[5]); seconds = int(sys.argv[6])
    cache_dir.mkdir(parents=True, exist_ok=True)
    guest_base, guest_size = pe_layout(exe)
    identity = hashlib.sha256(("HW-059\0qemu-arm-static\0chain=0\0" + sha(binary) + sha(exe)).encode()).hexdigest()
    results = []
    for pair in range(4):
        cache_file = cache_dir / "winx86-armv7-v3.bin"
        cache_file.unlink(missing_ok=True)
        cold = execute(binary, exe, croot, droot, seconds, cache_dir, identity, guest_base, guest_size, f"p{pair}-cold")
        warm = execute(binary, exe, croot, droot, seconds, cache_dir, identity, guest_base, guest_size, f"p{pair}-warm")
        if not cold["first_frame_hash"] or cold["first_frame_hash"] != warm["first_frame_hash"]:
            print(f"FAIL frame mismatch pair={pair + 1} cold={cold['first_frame_hash']} warm={warm['first_frame_hash']}", file=sys.stderr)
            raise SystemExit(1)
        cold_stats = cold["tcache"][0]
        warm_stats = warm["tcache"][0]
        results.append({"pair": pair + 1, "cache_bytes": cache_file.stat().st_size if cache_file.exists() else 0,
                        "cold": cold, "warm": warm,
                        "cache_reused": cold_stats[2] > 0 and warm_stats[0] > 0,
                        "first_frame_same": cold["first_frame_hash"] == warm["first_frame_hash"]})
    report = {"identity": identity, "seconds": seconds, "pairs": results,
              "cache_effective": all(row["cache_reused"] for row in results),
              "correctness": "PASS"}
    print(json.dumps(report, sort_keys=True))

if __name__ == "__main__":
    main()
