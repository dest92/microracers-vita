#!/usr/bin/env python3
"""El índice de bugs debe tener identidades únicas y enlaces coherentes."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
INDEX = ROOT / "hotwheels/BUGS.md"
text = INDEX.read_text(encoding="utf-8")
rows = re.findall(r"^\| \[(BUG-\d+)\]\(([^)]+)\) \|", text, re.M)
errors = []
indexed = set()
seen = set()
for bug_id, target in rows:
    if bug_id in seen:
        errors.append(f"ID duplicado: {bug_id}")
    seen.add(bug_id)
    indexed.add((INDEX.parent / target).resolve())
    detail = INDEX.parent / target
    if not detail.is_file():
        errors.append(f"{bug_id}: falta {target}")
        continue
    heading = detail.read_text(encoding="utf-8").splitlines()[0]
    if not re.match(r"^# BUG[- ]?" + re.escape(bug_id[4:]) + r"\b", heading):
        errors.append(f"{bug_id}: encabezado incoherente en {target}: {heading}")
# Invariante: cerrados históricos que no deben "des-cerrarse" por accidente.
# BUG-003 sale de la lista el 2026-10-21 (oc-qa): su fix del falso negativo sólo
# existe en el árbol de trabajo, así que en HEAD vuelve a estar abierto (BUG-053).
for bug_id in ("BUG-002", "BUG-004", "BUG-005", "BUG-007", "BUG-009"):
    matches = [INDEX.parent / target for row_id, target in rows if row_id == bug_id]
    if len(matches) != 1:
        errors.append(f"{bug_id}: se esperaba exactamente una fila de índice; hay {len(matches)}")
        continue
    metadata = next((line for line in matches[0].read_text(encoding="utf-8").splitlines()[:5] if re.search(r"Estado[: ]", line, re.I)), "")
    if not re.search(r"Estado:\s*\*{0,2}cerrado\b", metadata, re.I):
        errors.append(f"{bug_id}: el metadato vigente no dice cerrado")
for detail in (INDEX.parent / "bugs").glob("BUG-*.md"):
    if detail.resolve() not in indexed:
        errors.append(f"detalle sin fila de índice: {detail.relative_to(INDEX.parent)}")
if errors:
    print("\n".join(errors), file=sys.stderr)
    sys.exit(1)
print(f"BUG index: {len(rows)} IDs únicos, enlaces coherentes y detalles completos")
