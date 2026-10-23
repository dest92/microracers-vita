#!/usr/bin/env bash
# tools/soak.sh [min=30] [sw|d3d] — HW-027: soak menú+carrera bajo qemu sin crash/fuga.
# Criterios (todos deben cumplirse): (1) EVIDENCIA DE CIERRE: run.log termina con
# "shutdown requested exit=0x00000000" Y duración nominal cumplida (ELAPSED >=
# SECS-SOAK_DUR_TOL; cierre temprano = FAIL); sin esa línea es FAIL (crash,
# "salida no cero: N" típ.124=timeout, o run sin cierre); el rc del run_host por
# sí solo no se usa (un grep sin match lo deja en 1),
# (2) >=1 frame, (3) RSS máx. de qemu-arm < SOAK_MEM_MB (default 1536), (4) FDs máx.
# de qemu-arm < SOAK_FD_MAX (default 4096), (5) >=1 muestra válida (VmRSS leído;
# docker stats NO cuenta: es memoria de contenedor, columna aparte mem_cont_mb).
# Contenedor de este run por label hw_run=soak-<stamp> (HW_RUN_ID, aditivo en
# run_host.sh); overlay race-$MODE-settings.dat por HW_ENV si existe y no está fijado.
# Muestreo cada 15 s en out/soak-<stamp>/samples.csv (epoch_s,mem_qemu_mb,mem_cont_mb,
# fds_qemu,valida) + report.md. (6) Espacio libre >= SOAK_MIN_FREE_MB (default300):
# si cae, aborta el run (disco lleno mató un soak previo con rc134). (7) poda
# frame_*.png: SOAK_MAX_FRAMES (default200) TOTAL durante el run y
# SOAK_KEEP_FRAMES (default10) TOTAL al final (prune_frames reparte entre
# bordes; regresión en selftest con 250→200 y 250→10) — la poda es
# la cota en HEAD mientras la retención del presenter (C++) esté en WIP.
# Sale 0 si todo PASS; 1 si falla algún criterio. --selftest: valida parseo/veredicto.
set -uo pipefail
ulimit -c 0
HW="$(cd "$(dirname "$0")/.." && pwd)"
# Tolerancia del guard de duración (ver verdict_run): el timer de shutdown del
# guest arranca con el emulador (boot+script), no con T0 del sampler; en el run de
# referencia ELAPSED=1797 s para ventana nominal de 1800 s.
SOAK_DUR_TOL="${SOAK_DUR_TOL:-30}"

parse_mem() { # "49.7MiB / 62GiB" -> 49 (fallback docker stats)
  printf '%s' "$1" | awk '{u=$1; gsub(/[0-9.]/,"",u); v=$1+0;
    if(u=="GiB")v*=1024; else if(u=="TiB")v*=1048576; else if(u=="KiB")v/=1024;
    printf "%d", v}'
}
prune_frames() { # dir max_total — poda anti-fuga (BUG-023): conserva
  # `max_total` frames repartidos entre bordes (ceil de la mitad al inicio,
  # el resto al final) y borra el medio. El parámetro es TOTAL, no por borde
  # (re-review-HW-027 P2: con semántica "por borde" la cota 200 retenía 400).
  # Cota de espacio O(1) (defensa en profundidad junto a la retención del
  # presenter en C++).
  local dir="$1" total="${2:-10}" keep tail n start end
  [ -d "$dir" ] || return 0
  n=$(ls "$dir"/frame_*.png 2>/dev/null | wc -l) || return 0
  [ "$n" -gt "$total" ] || return 0
  keep=$(((total + 1) / 2)); tail=$((total - keep))
  start=$((keep + 1)); end=$((n - tail))
  ls "$dir"/frame_*.png | sed -n "${start},${end}p" | xargs -r rm -f
}

stop_run_container() { # id — cancela el contenedor del run y VERIFICA que no
  # quede ninguno activo. Ventana de creación (re-review-2484c4e P1): el CLI
  # `docker run` puede estar creando el contenedor mientras hacemos una única
  # consulta; `ps -q` (sin -a) ni siquiera ve los estado `created`. Por eso:
  # (a) `ps -aq` incluye creados/terminados, (b) se reintenta (40×0.5 s) hasta
  # que la verificación final devuelva vacío, (c) `rm -f` mata la creación
  # pendiente para que el CLI huérfano de run_host falle en start.
  # rc de `docker ps` distingue "daemon no responde" (error → reintentar, nunca
  # confirmar) de "consulta exitosa vacía" (ausencia confirmada → return 0).
  # Un `|| true` aquí convertiría un daemon caído en cancelación confirmada
  # (CAMBIOS P1 review-ae98b8d). Sin verificar tras agotar intentos → return 1.
  local id="$1" tries="${SOAK_STOP_TRIES:-40}" pids rc
  [ -n "$id" ] || return 0
  # sin el binario docker no se puede verificar nada: fallo conservador (CAMBIOS
  # P2 re-review-de1992d-bugs) — abort_disk debe informar "NO confirmado".
  command -v docker >/dev/null 2>&1 || return 1
  while [ "$tries" -gt 0 ]; do
    pids=$(docker ps -aq --filter "label=hw_run=$id" 2>/dev/null); rc=$?
    if [ "$rc" -eq 0 ]; then
      if [ -n "$pids" ]; then
        docker stop -t 5 $pids 2>/dev/null || docker kill $pids 2>/dev/null || true
        docker rm -f $pids >/dev/null 2>&1 || true
      elif ! pgrep -f "docker run.*hw_run=$id" >/dev/null 2>&1; then
        # vacío + daemon OK + CLI del run ausente: el CLI es quien podría crear
        # el contenedor "después" (carrera created); si ya no existe, confirmado.
        return 0
      fi
    fi
    tries=$((tries - 1)); sleep 0.5
  done
  return 1
}

abort_disk() { # fmb — umbral de disco: para wrapper, CLI de docker y contenedor
  echo "== aborto: disco ${1:-?}MB < ${SOAK_MIN_FREE_MB}MB" >> "$OUT/run_host.stdout"
  kill "$RUN_PID" 2>/dev/null || true
  # CLI huérfano de run_host (el wrapper no tiene trap): id de run único
  # (nanos+pid), el pattern no puede matchear runs ajenos.
  pkill -f "docker run.*hw_run=${HW_RUN_ID:-__sin_label__}" 2>/dev/null || true
  stop_run_container "${HW_RUN_ID:-}" \
    || echo "== aborto: contenedor del run NO confirmado detenido" >> "$OUT/run_host.stdout"
}

verdict_fin() { # 0 si shutdown limpio, 1 si crash/falta
  case "$1" in *"shutdown requested exit=0x00000000"*) return 0;; *) return 1;; esac
}
verdict_run() { # FIN FAULT SALIDA_RC ELAPSED SECS -> globales VF_FAIL y RUN_FIN_MSG.
  # PASS sólo con: (a) shutdown limpio sin rc del wrapper, o (b) rc124 (timeout del
  # wrapper) sin FIN y ELAPSED>=SECS (duración cumplida). FIN+rc124 = teardown
  # colgado → FAIL; rc!=124 o duración corta → FAIL.
  local fin="${1:-}" fault="${2:-}" salida="${3:-}" el="${4:-0}" secs="${5:-0}"
  VF_FAIL=""; RUN_FIN_MSG="$fin"
  if [ -n "$salida" ] && [ "$salida" != 124 ]; then
    VF_FAIL="wrapper salida no cero: $salida (qemu no cerró solo)"; return 0; fi
  if [ "$salida" = 124 ] && verdict_fin "$fin"; then
    VF_FAIL="wrapper salida no cero: 124 con FIN limpio (teardown colgado)"; return 0; fi
  if [ "$salida" = 124 ] && [ -n "$fin" ]; then
    VF_FAIL="crash: ${fin#[hw] fin: } (y rc124)"; return 0; fi
  if [ "$salida" = 124 ]; then
    if [ "${el:-0}" -ge "${secs:-0}" ]; then
      RUN_FIN_MSG="timeout del soak tras ${el}s >= ${secs}s (sin fault; duración cumplida)"
      return 0
    fi
    VF_FAIL="wrapper rc124 con duración incompleta (${el}s < ${secs}s)"; return 0; fi
  if verdict_fin "$fin"; then
    # Duración mínima también para el cierre limpio: un guest que se apague a los
    # pocos segundos no satisface un soak de N min (CAMBIOS P1 de re-review).
    if [ "${secs:-0}" -le 0 ]; then
      VF_FAIL="cierre sin duración mínima especificada"; return 0; fi
    local min=$((secs - SOAK_DUR_TOL)); [ "$min" -lt 0 ] && min=0
    [ "${el:-0}" -ge "$min" ] && return 0
    VF_FAIL="cierre limpio antes de la duración mínima (${el}s < ${secs}s - ${SOAK_DUR_TOL}s de tolerancia)"
    return 0
  fi
  if [ -n "$fin" ]; then VF_FAIL="crash: ${fin#[hw] fin: }"; return 0; fi
  if [ -n "$fault" ]; then VF_FAIL="crash sin fin: $fault"; return 0; fi
  if [ -n "$RUNDIR" ]; then VF_FAIL="run sin shutdown limpio (sin fin/fault/rc)"; return 0; fi
  VF_FAIL="run no localizado (sin run propio)"
}
if [ "${1:-}" = "--selftest" ]; then
  rc=0
  [ "$(parse_mem '50GiB')" = 51200 ] || { echo "selftest parse GiB"; rc=1; }
  [ "$(parse_mem '49.7MiB / 62GiB')" = 49 ] || { echo "selftest parse MiB"; rc=1; }
  [ "$(parse_mem '512KiB')" = 0 ] || { echo "selftest parse KiB"; rc=1; }
  verdict_fin "[hw] fin: shutdown requested exit=0x00000000 frames=10 missing=0" || { echo "selftest verdict ok"; rc=1; }
  verdict_fin "[hw] fin: box86 dynarec fault exit=0xc0000005 frames=223 missing=0" && { echo "selftest verdict crash"; rc=1; }
  verdict_fin "" && { echo "selftest verdict vacio"; rc=1; }
  chk() { # $1=VF_FAIL esperado (-=vacío) $2=etiqueta
    local want="$1" tag="$2"; [ "$VF_FAIL" = "$want" ] || { echo "selftest run $tag (VF_FAIL='$VF_FAIL')"; rc=1; }
  }
  RUNDIR=/tmp/x
  verdict_run '[hw] fin: shutdown requested exit=0x00000000 frames=10 missing=0' '' '' 1797 1800; chk '' fin_ok
  verdict_run '[hw] fin: shutdown requested exit=0x00000000 frames=10 missing=0' '' '' 100 1800; case "$VF_FAIL" in "cierre limpio antes de la duración mínima"*) ;; *) echo "selftest fin_corto (VF_FAIL='$VF_FAIL')"; rc=1;; esac
  verdict_run '[hw] fin: shutdown requested exit=0x00000000 frames=10 missing=0' ''; case "$VF_FAIL" in "cierre sin duración mínima especificada"*) ;; *) echo "selftest fin_sin_duracion (VF_FAIL='$VF_FAIL')"; rc=1;; esac
  verdict_run '[hw] fin: box86 dynarec fault exit=0xc0000005' ''; case "$VF_FAIL" in crash:*) ;; *) echo "selftest run fin_crash"; rc=1;; esac
  verdict_run '' 'aborting emulation' ''; case "$VF_FAIL" in "crash sin fin:"*) ;; *) echo "selftest run fault"; rc=1;; esac
  verdict_run '' '' '134' 100 100; case "$VF_FAIL" in "wrapper salida no cero: 134"*) ;; *) echo "selftest rc!=124"; rc=1;; esac
  verdict_run '[hw] fin: shutdown requested exit=0x00000000 frames=10 missing=0' '' '124' 9999 1800; case "$VF_FAIL" in *teardown*) ;; *) echo "selftest fin+rc124"; rc=1;; esac
  verdict_run '' '' '124' 1800 1800; chk '' timeout_ok_duracion
  verdict_run '' '' '124' 100 1800; case "$VF_FAIL" in "wrapper rc124 con duración incompleta"*) ;; *) echo "selftest rc124 corto"; rc=1;; esac
  RUNDIR=/tmp/x; verdict_run '' ''; case "$VF_FAIL" in "run sin shutdown"*) ;; *) echo "selftest run sin cierre"; rc=1;; esac
  RUNDIR=; verdict_run '' ''; case "$VF_FAIL" in "run no localizado"*) ;; *) echo "selftest run sin directorio"; rc=1;; esac
  # regresión (review-1f876f8 + re-review-HW-027 P2): la cota es TOTAL
  # (200 retiene 200, no 400) y se conservan los bordes elegidos.
  TD=$(mktemp -d)
  for i in $(seq 1 250); do : > "$TD/frame_$(printf '%04d' "$i").png"; done
  prune_frames "$TD" 200
  [ "$(ls "$TD" | wc -l)" = 200 ] || { echo "selftest prune cuota total (250→$(ls "$TD" | wc -l))"; rc=1; }
  [ -f "$TD/frame_0001.png" ] && [ -f "$TD/frame_0100.png" ] || { echo "selftest prune head200"; rc=1; }
  [ -f "$TD/frame_0151.png" ] && [ -f "$TD/frame_0250.png" ] || { echo "selftest prune tail200"; rc=1; }
  [ ! -f "$TD/frame_0125.png" ] || { echo "selftest prune medio200"; rc=1; }
  prune_frames "$TD" 10
  [ "$(ls "$TD" | wc -l)" = 10 ] || { echo "selftest prune cuota 10→$(ls "$TD" | wc -l)"; rc=1; }
  [ -f "$TD/frame_0001.png" ] && [ -f "$TD/frame_0005.png" ] || { echo "selftest prune head"; rc=1; }
  [ -f "$TD/frame_0246.png" ] && [ -f "$TD/frame_0250.png" ] || { echo "selftest prune tail"; rc=1; }
  [ ! -f "$TD/frame_0125.png" ] || { echo "selftest prune medio"; rc=1; }
  rm -rf "$TD"
  # regresión (re-review-cb556a7 P2): el aborto por disco mata el wrapper y
  # detiene el contenedor del run (docker stop por label), sin dejar proceso.
  FAKE=$(mktemp -d)
  # --- caso A: ventana created + CLI aún vivo + stop efectivo ---
  # fake docker: 1ª ps vacía, después cid123; `stop` mata al qemu modelo.
  # fake pkill: no mata al modelo CLI (para poder observar el reintento por CLI).
  # Modelo CLI con argv0 que matchea "docker run.*hw_run=$id" y muere a 0.4 s.
  cat > "$FAKE/docker" <<'EOS'
#!/usr/bin/env bash
printf 'docker %s\n' "$*" >> "$DOCKER_LOG"
case "$1" in
  ps)
    [ -f "$DOCKER_LOG.stopped" ] && exit 0
    if [ ! -f "$DOCKER_LOG.seen" ]; then : > "$DOCKER_LOG.seen"; exit 0; fi
    printf 'cid123\n';;
  stop|kill|rm)
    [ -n "${FAKE_QEMU_PID:-}" ] && kill "$FAKE_QEMU_PID" 2>/dev/null
    : > "$DOCKER_LOG.stopped";;
esac
exit 0
EOS
  printf '#!/usr/bin/env bash\nprintf "pkill %%s\\n" "$*" >> "$DOCKER_LOG"\nexit 1\n' > "$FAKE/pkill"
  chmod +x "$FAKE/docker" "$FAKE/pkill"
  export DOCKER_LOG="$FAKE/docker.log"; : > "$DOCKER_LOG"
  PATH="$FAKE:$PATH"
  OUT="$FAKE/out"; mkdir -p "$OUT"
  HW_RUN_ID="soak-selftest"; SOAK_MIN_FREE_MB=300; SOAK_STOP_TRIES=10
  sleep 60 & RUN_PID=$!                       # modelo wrapper run_host
  sleep 60 & export FAKE_QEMU_PID=$!          # modelo qemu en el contenedor
  bash -c 'exec -a "docker run hw_run=soak-selftest" sleep 0.4' &
  abort_disk 10
  grep -q '^docker ps -aq --filter label=hw_run=soak-selftest$' "$DOCKER_LOG" || { echo "selftest abort ps -aq"; rc=1; }
  grep -q '^docker stop -t 5 cid123$' "$DOCKER_LOG" || { echo "selftest abort docker stop (log='$(cat "$DOCKER_LOG")')"; rc=1; }
  [ "$(grep -c '^docker ps' "$DOCKER_LOG")" -ge 3 ] || { echo "selftest abort sin reintento por CLI vivo"; rc=1; }
  kill -0 "$RUN_PID" 2>/dev/null && { echo "selftest abort wrapper vivo"; rc=1; }
  kill -0 "$FAKE_QEMU_PID" 2>/dev/null && { echo "selftest abort qemu vivo (stop inefectivo)"; rc=1; }
  unset FAKE_QEMU_PID
  rm -rf "$FAKE"
  # --- caso B: daemon caído (review-ae98b8d): ps siempre rc1 → NO confirmar ---
  FAKE2=$(mktemp -d)
  printf '#!/usr/bin/env bash\nprintf "docker %%s\\n" "$*" >> "$DOCKER_LOG"\nexit 1\n' > "$FAKE2/docker"
  chmod +x "$FAKE2/docker"
  export DOCKER_LOG="$FAKE2/docker.log"; : > "$DOCKER_LOG"
  PATH="$FAKE2:$PATH"
  OUT="$FAKE2/out"; mkdir -p "$OUT"
  HW_RUN_ID="soak-selftest"; SOAK_MIN_FREE_MB=300; SOAK_STOP_TRIES=2
  sleep 60 & RUN_PID=$!
  abort_disk 10
  grep -q 'NO confirmado detenido' "$OUT/run_host.stdout" || { echo "selftest abort daemon caido sin constancia"; rc=1; }
  kill -0 "$RUN_PID" 2>/dev/null && { echo "selftest abort daemon wrapper vivo"; rc=1; }
  rm -rf "$FAKE2"; unset DOCKER_LOG; SOAK_STOP_TRIES=40
  # --- borde: sin binario docker → fallo conservador, nunca confirmar ---
  FAKE3=$(mktemp -d)
  if PATH="$FAKE3" command -v docker >/dev/null 2>&1; then
    echo "selftest sin-docker: no se pudo aislar docker"; rc=1
  elif PATH="$FAKE3" stop_run_container "soak-selftest"; then
    echo "selftest sin-docker retorno 0 (deberia fallar)"; rc=1
  fi
  rm -rf "$FAKE3"
  [ "$rc" = 0 ] && echo "SOAK selftest: OK (parse_mem, prune_frames, verdict_fin, verdict_run, abort_disk)"
  exit "$rc"
fi

MIN="${1:-30}"; MODE="${2:-sw}"
case "$MODE" in sw|d3d) ;; *) echo 'modo debe ser sw o d3d' >&2; exit 2;; esac
SECS=$((MIN * 60))
MEM_MAX_MB="${SOAK_MEM_MB:-1536}"; FD_MAX="${SOAK_FD_MAX:-4096}"
SOAK_MIN_FREE_MB="${SOAK_MIN_FREE_MB:-300}"
STAMP="$(date +%Y%m%d-%H%M%S%N)-$$"  # nanosegundos+pid: sin colisión entre runs simultáneos
OUT="$HW/out/soak-$STAMP"; mkdir -p "$OUT"
CSV="$OUT/samples.csv"; echo "epoch_s,mem_qemu_mb,mem_cont_mb,fds_qemu,valida" > "$CSV"
export HW_RUN_ID="soak-$STAMP"
export HW_KEEP_OUT=1
export HW_ENV="${HW_ENV:-} HW_INPUT_SCRIPT=/work/hotwheels/tools/race_enter.script"
[ "$MODE" = d3d ] && export HW_ENV="$HW_ENV HW_D3D=1"
case "$HW_ENV" in
  *HW_SETTINGS_FILE=*) ;;
  *) [ -f "$HW/tools/race-$MODE-settings.dat" ] &&
       export HW_ENV="$HW_ENV HW_SETTINGS_FILE=/work/hotwheels/tools/race-$MODE-settings.dat";;
esac

# run_host escribe su dir de run en HW_RUN_DIR_FILE (aditivo); se espera para poder
# limpiar frames DURANTE el run (con el stdout tardaríamos hasta el final).
export HW_RUN_DIR_FILE="$OUT/run_dir"
RUNDIR=""
"$HW/tools/run_host.sh" "$SECS" "$MODE" > "$OUT/run_host.stdout" 2>&1 &
RUN_PID=$!
for _ in $(seq 1 120); do
  [ -s "$OUT/run_dir" ] && { RUNDIR="$(cat "$OUT/run_dir")"; break; }
  kill -0 "$RUN_PID" 2>/dev/null || break
  sleep 1
done
MAX_MEM=0; MAX_CONT=0; MAX_FD=0; SAMPLES=0; VALIDS=0
T0=$(date +%s)
while kill -0 "$RUN_PID" 2>/dev/null; do
  CID="$(docker ps -q --filter "label=hw_run=$HW_RUN_ID" --filter ancestor=hw-host-arm:24.04 2>/dev/null | head -1)"
  MEM=0; CONT=0; FD=0; OKM=0
  if [ -n "$CID" ]; then
    V="$(docker exec "$CID" sh -c 'p=$(pgrep -x qemu-arm | head -1); [ -n "$p" ] && grep -m1 VmRSS /proc/$p/status || true' 2>/dev/null || true)"
    if printf '%s' "$V" | grep -q 'VmRSS:'; then
      MEM=$(( $(printf '%s' "$V" | awk '/VmRSS:/{print $2}') / 1024 )); OKM=1
    fi
    RAW="$(docker stats --no-stream --format '{{.MemUsage}}' "$CID" 2>/dev/null | head -1)"
    [ -n "$RAW" ] && CONT="$(parse_mem "$RAW")"
    FD="$(docker exec "$CID" sh -c 'p=$(pgrep -x qemu-arm | head -1); [ -n "$p" ] && ls /proc/$p/fd 2>/dev/null | wc -l || echo 0' 2>/dev/null || echo 0)"
    [ -z "$FD" ] && FD=0
    [ "$OKM" -eq 1 ] && VALIDS=$((VALIDS + 1))
  fi
  [ "$MEM" -gt "$MAX_MEM" ] 2>/dev/null && MAX_MEM=$MEM
  [ "$CONT" -gt "$MAX_CONT" ] 2>/dev/null && MAX_CONT=$CONT
  [ "$FD" -gt "$MAX_FD" ] 2>/dev/null && MAX_FD=$FD
  echo "$(($(date +%s) - T0)),$MEM,$CONT,$FD,$OKM" >> "$CSV"
  SAMPLES=$((SAMPLES + 1))
  prune_frames "$RUNDIR" "${SOAK_MAX_FRAMES:-200}"   # cota durante el run (BUG-023)
  FMB=$(df -Pm "$HW" | awk 'NR==2{print $4}')
  if [ "$FMB" -lt "$SOAK_MIN_FREE_MB" ]; then
    abort_disk "$FMB"
    break
  fi
  sleep 15
done
wait "$RUN_PID" 2>/dev/null
# El run propio: run_host imprime "== <path>/out/run-<stamp>"; out/latest es global
# y lo pisan runs simultáneos de otros agentes.
[ -z "$RUNDIR" ] && RUNDIR="$(grep -m1 '^== .*out/run-' "$OUT/run_host.stdout" 2>/dev/null | sed 's/^== //' || true)"
LOG="$RUNDIR/run.log"
FRAMES=$(ls "$RUNDIR"/frame_*.png 2>/dev/null | wc -l)
FIN="$(grep -h '\[hw\] fin:' "$LOG" 2>/dev/null | tail -1)"
FAULT="$(grep -Eih 'aborting emulation|dynarec fault|SIGSEGV|segfault|Assertion .* failed|dumped core' "$OUT/run_host.stdout" "$LOG" "$RUNDIR/stdout.txt" 2>/dev/null | head -1 || true)"
# run_host imprime "== salida no cero: N" al stdout.txt del run (y el tail puede
# replicarlo); N=124 es qemu terminado por timeout SECS+20 sin cerrar.
SALIDA="$(grep -hoE 'salida no cero: [0-9]+' "$RUNDIR/stdout.txt" "$OUT/run_host.stdout" 2>/dev/null | head -1 | awk '{print $NF}' || true)"
case "$SALIDA" in ''|*[!0-9]*) SALIDA="";; esac  # stdout truncado por disco no vale como rc
ELAPSED=$(($(date +%s) - T0))
verdict_run "$FIN" "$FAULT" "$SALIDA" "$ELAPSED" "$SECS"
FAIL="$VF_FAIL"; FIN="$RUN_FIN_MSG"
# Criterio (6): no corromperse más si el disco quedó sin espacio
FREE_MB=$(df -Pm "$HW" | awk 'NR==2{print $4}')
[ "$FREE_MB" -lt "$SOAK_MIN_FREE_MB" ] && FAIL="${FAIL:+$FAIL }disco ${FREE_MB}MB<${SOAK_MIN_FREE_MB}MB"
# Criterio (7): conservar SOAK_KEEP_FRAMES primeros/últimos del run propio
if [ "${SOAK_KEEP_FRAMES:-10}" -ge 0 ]; then prune_frames "$RUNDIR" "${SOAK_KEEP_FRAMES:-10}"; fi
[ "$FRAMES" -eq 0 ] && FAIL="$FAIL sin frames"
[ "$MAX_MEM" -ge "$MEM_MAX_MB" ] && FAIL="$FAIL RSS ${MAX_MEM}MB>=${MEM_MAX_MB}"
[ "$MAX_FD" -ge "$FD_MAX" ] && FAIL="$FAIL FDs ${MAX_FD}>=${FD_MAX}"
[ "$VALIDS" -eq 0 ] && FAIL="$FAIL sin muestras de RSS válidas"
VERDICT=PASS; [ -n "$FAIL" ] && VERDICT="FAIL:$FAIL"
REPORT="$OUT/report.md"
{
  echo "# Soak HW-027 — $STAMP (${MIN} min, modo $MODE)"
  echo "- Verdict: **$VERDICT**"
  echo "- fin: ${FIN:-<ninguno>}"
  echo "- frames=$FRAMES, muestras=$SAMPLES, válidas=$VALIDS (cada 15 s, VmRSS de qemu-arm)"
  echo "- RSS qemu (VmRSS) máx ${MAX_MEM} MB (umbral $MEM_MAX_MB), FDs qemu máx $MAX_FD (umbral $FD_MAX)"
  echo "- RSS contenedor máx ${MAX_CONT} MB (informativo; NO cuenta para el criterio)"
  if [ -z "$FAIL" ]; then
    echo "- Run del juego: artefactos eliminados tras PASS"
  else
    echo "- Run del juego: ${RUNDIR:-<desconocido>} (conservado para debug)"
  fi
  echo "- Muestreo: samples.csv (epoch_s,mem_qemu_mb,mem_cont_mb,fds_qemu,valida)"
} > "$REPORT"
echo "== $VERDICT (frames=$FRAMES RSSmax=${MAX_MEM}MB FDmax=$MAX_FD validas=$VALIDS) -> $REPORT"
if [ -z "$FAIL" ] && [ -n "$RUNDIR" ] && [ -d "$RUNDIR" ]; then
  [ "$(readlink "$HW/out/latest" 2>/dev/null || true)" = "$(basename "$RUNDIR")" ] && rm -f "$HW/out/latest"
  rm -rf -- "$RUNDIR"
fi
bash "$HW/tools/prune_out.sh" >/dev/null 2>&1 || true
[ -z "$FAIL" ]
