#!/usr/bin/env python3
"""Historical JSON schema sketch; runtime uses WXTCARM1 from SPEC-045."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile

FORMAT = 1
MAX_CACHE = 64 * 1024 * 1024


def _canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def write_cache(path, exe_path, translator, target, blocks):
    data = {
        "format": FORMAT,
        "exe_sha256": hashlib.sha256(Path(exe_path).read_bytes()).hexdigest(),
        "translator": translator,
        "target": target,
        "blocks": blocks,
    }
    data["payload_sha256"] = hashlib.sha256(_canonical(data)).hexdigest()
    encoded = _canonical(data)
    if len(encoded) > MAX_CACHE:
        raise ValueError("cache exceeds size limit")
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def load_cache(path, exe_path, translator, target, source_hashes=None):
    raw = Path(path).read_bytes()
    if len(raw) > MAX_CACHE:
        return None
    try:
        data = json.loads(raw)
        digest = data.pop("payload_sha256")
        if data.get("format") != FORMAT or hashlib.sha256(_canonical(data)).hexdigest() != digest:
            return None
        if data.get("exe_sha256") != hashlib.sha256(Path(exe_path).read_bytes()).hexdigest():
            return None
        if data.get("translator") != translator or data.get("target") != target:
            return None
        accepted = []
        for block in data.get("blocks", []):
            code = bytes.fromhex(block["arm_code_hex"])
            if (not code or len(code) % 4 or not 0 <= block["guest_eip"] <= 0xffffffff or
                    len(block["source_sha256"]) != 64):
                return None
            for reloc in block["relocations"]:
                if reloc["offset"] < 0 or reloc["offset"] + 4 > len(code):
                    return None
                if reloc["kind"] not in ("helper", "emu_slot", "block_link") or reloc["symbol"] < 0:
                    return None
            expected_source = (source_hashes or {}).get(block["guest_eip"])
            if source_hashes is None or expected_source == block["source_sha256"]:
                accepted.append(block)
        data["blocks"] = accepted
        return data
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cache")
    parser.add_argument("exe")
    parser.add_argument("--translator", default="wx86-proto-1")
    parser.add_argument("--target", default="armv7-neon-le")
    parser.add_argument("--source-hashes", help="JSON map EIP decimal → SHA-256 de bytes guest actuales")
    args = parser.parse_args()
    source_hashes = None
    if args.source_hashes:
        source_hashes = {int(k, 0): v for k, v in json.loads(Path(args.source_hashes).read_text()).items()}
    cache = load_cache(args.cache, args.exe, args.translator, args.target, source_hashes)
    if cache is None:
        print("miss")
    elif source_hashes is None:
        print("metadata-valid; block bytes unchecked")
    else:
        print("hit" if cache["blocks"] else "miss")


if __name__ == "__main__":
    main()
