#!/usr/bin/env bash
# smoke_qemu_verdict.sh <latest_dir> — decide si la etapa qemu-arm de smoke.sh
# fue SANA. Extraído de smoke.sh para poder testearlo sin Docker (BUG-003).
#
# Criterio (BUG-003, corregido): la corrida es OK si y sólo si
#   run.log tiene [hw] base=  AND  frames > 0  AND
#   run.log tiene "[hw] fin: shutdown requested" (apagado limpio)  AND
#   NO tiene "salida forzada" (el scheduler no paró en 3 s -> cuelgue real).
# NO se usa "GIL busy" como fallo: es un diagnóstico benigno del sampler
# (toma la foto mientras un shim tiene el GIL); aparece en corridas limpias.
# El rc de run_host.sh NO decide (hoy es espurio por su set -e + grep sin match).
#
# Salida (3 líneas):
#   1) OK|FAIL
#   2) motivo (base/frames/fin/forzada + load del host)
#   3) última trampa observada ([tag N] ...) o "(sin tags de imports)"
set -uo pipefail

D="${1:?uso: smoke_qemu_verdict.sh <latest_dir>}"

base=0;    grep -qa '\[hw\] base=' "$D/run.log" 2>/dev/null && base=1
clean=0;   grep -qa 'fin: shutdown requested' "$D/run.log" 2>/dev/null && clean=1
forced=0;  grep -qa 'salida forzada' "$D/run.log" 2>/dev/null && forced=1
frames="$(ls "$D"/frame_*.png 2>/dev/null | wc -l)"
# Carga del host al emitir el veredicto (BUG-054: con loadavg alto el watchdog de
# shutdown puede dar "salida forzada" con el guest sano; el motivo lo deja escrito).
load="$(cut -d' ' -f1 /proc/loadavg 2>/dev/null || true)"
[ -n "$load" ] || load="?"
trap="$(grep -a '\[tag' "$D/stdout.txt" 2>/dev/null | tail -1 | sed -E 's/[[:space:]]*->.*$//')"
[ -n "$trap" ] || trap="(sin tags de imports)"

if [ "$base" = 1 ] && [ "$frames" -gt 0 ] && [ "$clean" = 1 ] && [ "$forced" = 0 ]; then
    echo "OK"
    echo "base=1 frames=$frames fin=limpio load=$load"
    echo "$trap"
    exit 0
fi

echo "FAIL"
echo "base=$base frames=$frames fin=$clean forzada=$forced load=$load"
echo "$trap"
exit 1
