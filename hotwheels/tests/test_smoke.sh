#!/usr/bin/env bash
# tests/test_smoke.sh — regresión de BUG-003: la etapa qemu-arm de smoke.sh NO
# debe dar OK con 0 frames ni con cuelgue real (salida forzada), pero SÍ debe dar
# OK en una corrida limpia aunque aparezca el diagnóstico benigno "GIL busy".
# Testea tools/smoke_qemu_verdict.sh con directorios sintéticos (sin Docker).
set -u
HW="$(cd "$(dirname "$0")/.." && pwd)"
VERDICT="$HW/tools/smoke_qemu_verdict.sh"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0
note() { printf '  %-30s %s\n' "$1" "$2"; }

# mk <nombre> <base:0|1> <frames> <run.log extra (multilínea)> <stdout>
mk() {
    local d="$TMP/$1"; mkdir -p "$d"
    [ "$2" = 1 ] && echo '[hw] base=0x00400000 entry=0x00452f05 imports=94' > "$d/run.log" || : > "$d/run.log"
    [ -n "${4:-}" ] && printf '%s\n' "$4" >> "$d/run.log"
    local i; for i in $(seq 1 "$3"); do : > "$d/frame_$i.png"; done
    [ -n "${5:-}" ] && printf '%s\n' "$5" > "$d/stdout.txt"
    echo "$d"
}
check() { # nombre esperado_rc dir [trap_esperada]
    local got out verdict trap
    if out="$(bash "$VERDICT" "$3" 2>/dev/null)"; then got=0; else got=$?; fi
    verdict="$(printf '%s\n' "$out" | sed -n '1p')"
    trap="$(printf '%s\n' "$out" | sed -n '3p')"
    if [ "$got" -eq "$2" ]; then pass=$((pass+1)); note "$1" "OK (rc=$got, $verdict)"
    else fail=$((fail+1)); note "$1" "FALLA: rc=$got esperado $2"; fi
    if [ -n "${4:-}" ] && [ "$trap" != "$4" ]; then
        fail=$((fail+1)); note "$1 (trampa)" "FALLA: '$trap' != '$4'"
    fi
}
FIN='[hw] fin: shutdown requested exit=0x00000000 frames=N missing=0'
HANG='[hw] muestra EIP: 0c3ffff0
[hw] el scheduler no paró en 3 s: salida forzada'

# 1) BUG-003: cuelga, 0 frames, salida forzada -> FAIL.
D=$(mk hang 1 0 "$HANG"); check "cuelgue+0 frames" 1 "$D"
# 2) Base, 0 frames pero apagado limpio -> FAIL (0 frames es rojo).
D=$(mk zeroframes 1 0 "$FIN" '[tag 45] KERNEL32.dll!GetLastError -> eax=0x2'); check "base+0 frames" 1 "$D"
# 3) Dos PNG retenidos pero salida forzada (no cerró) -> FAIL.
D=$(mk forced 1 2 "$HANG"); check "PNG+forzada" 1 "$D"
# 4) Sin base -> FAIL.
D=$(mk nobase 0 3 "$FIN"); check "sin base" 1 "$D"
# 5) Limpio con el diagnóstico benigno "GIL busy" -> OK (no es falso rojo).
D=$(mk gil 1 2 "[hw]   sched-native: GIL busy — racy snapshot follows
$FIN" '[tag 7] KERNEL32.dll!GetVersion -> eax=0x1'); check "limpio + GIL busy" 0 "$D" '[tag 7] KERNEL32.dll!GetVersion'
# 6) Limpio sin tags de imports -> OK.
D=$(mk notag 1 2 "$FIN"); check "limpio sin tags" 0 "$D" '(sin tags de imports)'

echo "== test_smoke: $pass ok, $fail fallas"
[ "$fail" -eq 0 ]
