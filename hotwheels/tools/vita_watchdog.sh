#!/usr/bin/env bash
# Watchdog de consola real para Hot Wheels (modo autónomo).
# Detecta juego trabado y lo reinicia: quit all + launch. Si VitaCompanion no responde, reboot.
# Señal de vida: el log del juego en ux0:data/hotwheels/ crece (o su mtime cambia) cada <=STALL s.
# Uso: vita_watchdog.sh [TITLEID] [STALL_s]    Log: hotwheels/out/watchdog.log
IP=${VITA_IP:?set VITA_IP to your Vita address}; TID=${1:-HWMR00002}; STALL=${2:-90}
F="ftp://$IP:1337/"; LOGDIR="ux0:/data/hotwheels"; OUT=${HW_WATCHDOG_OUT:-$HOME/vita-ports/hotwheels/out}; mkdir -p "$OUT"
W="$OUT/watchdog.log"; log(){ echo "$(date +%F' '%T) $*" | tee -a "$W"; }
console_locked(){ [ -e "$OUT/CONSOLE_LOCKED" ]; }
cmd(){
  if console_locked; then log "CONSOLE_LOCKED presente: comando '$1' suprimido"; return 1; fi
  local response status
  response=$(timeout 10 bash -c "exec 3<>/dev/tcp/$IP/1338; printf '%s\n' \"$1\" >&3; timeout 3 cat <&3" 2>/dev/null)
  status=$?
  printf '%s' "$response" | tr -d '\r'
  return "$status"
}
alive(){ cmd version | grep -qi companion; }
sig(){ timeout 20 curl -s "${F}${LOGDIR}/" | tr -d '\r' | awk '$NF=="hotwheels.log"{print $5,$6,$7,$8,$9}' | sort | md5sum | cut -c1-12; }
# Spec 022/BUG-026: "Launched." no prueba proceso nuevo (una instancia trabada sobrevive
# a quit all y launch la reanuda). Sólo cuenta boot_probe.txt fresco con "main"; si falta → reboot.
probe_fresh(){ ! console_locked && timeout 20 curl -s "${F}${LOGDIR}/boot_probe.txt" 2>/dev/null | grep -q '^main'; }
launch_probe(){ console_locked && return 1
  timeout 20 curl -s "${F}" -Q "DELE ${LOGDIR}/boot_probe.txt" >/dev/null 2>&1
  console_locked && return 1
  cmd "launch $TID" >/dev/null || return 1; sleep 25; probe_fresh; }
restart(){ console_locked && { log "CONSOLE_LOCKED presente: reinicio suprimido"; return 1; }
  log "REINICIO juego ($1)"; cmd "quit all" >/dev/null; sleep 4
  console_locked && { log "CONSOLE_LOCKED apareció durante reinicio; reboot suprimido"; return 1; }
  launch_probe && return
  console_locked && { log "CONSOLE_LOCKED apareció antes de reboot; reboot suprimido"; return 1; }
  log "launch sin sonda fresca (instancia trabada) → reboot"; cmd "quit all" >/dev/null
  console_locked && { log "CONSOLE_LOCKED apareció antes de reboot; reboot suprimido"; return 1; }
  cmd reboot >/dev/null || return 1; sleep 90
  locked "reboot por instancia trabada"; }
# BUG-035: tras reboot la Vita queda en pantalla de bloqueo y suspende toda app a
# ~15 s del launch. Relanzar sólo genera zombis: marcar y esperar al humano.
locked(){ echo "$1 $(date +%F' '%T)" > "$OUT/CONSOLE_LOCKED"; log "CONSOLE_LOCKED ($1): requiere desbloqueo humano; borrar el flag al desbloquear"; }
watchdog_startup(){
  log "watchdog start TID=$TID STALL=${STALL}s"
  while console_locked; do log "CONSOLE_LOCKED presente: espero desbloqueo humano sin enviar comandos"; sleep 15; done
  cmd "nosleep on" >/dev/null
  last=$(sig); t=$(date +%s); fails=0; restarts=0
}
watchdog_cycle(){
  sleep 15
  if console_locked; then
    log "CONSOLE_LOCKED presente: espero desbloqueo humano sin sondear/rebootear"
    t=$(date +%s); return 0
  fi
  if ! alive; then
    if console_locked; then log "CONSOLE_LOCKED apareció durante sondeo; espero desbloqueo"; t=$(date +%s); return 0; fi
    fails=$((fails+1)); log "VitaCompanion no responde ($fails)"
    if [ $fails -ge 4 ]; then
      console_locked && return 0
      log "consola colgada → intento reboot"; cmd reboot >/dev/null || return 0; sleep 90
      if console_locked; then log "CONSOLE_LOCKED apareció tras reboot; no relanzo"; t=$(date +%s)
      elif alive; then cmd "nosleep on" >/dev/null; locked "reboot por VitaCompanion mudo"; fails=0
      else log "SIN RESPUESTA tras reboot: consola CONGELADA, requiere intervención física. Agentes siguen en Vita3K/qemu."; touch "$OUT/CONSOLE_DOWN"; sleep 300; fi
    fi
    return 0
  fi
  if console_locked; then log "CONSOLE_LOCKED apareció durante sondeo; espero desbloqueo"; t=$(date +%s); return 0; fi
  fails=0; rm -f "$OUT/CONSOLE_DOWN"
  [ -e "$OUT/CONSOLE_BUSY" ] && { t=$(date +%s); return 0; }
  console_locked && { t=$(date +%s); return 0; }
  if [ -z "$(timeout 20 curl -s "${F}${LOGDIR}/" | tr -d '\r' | grep -E ' hotwheels\.log$')" ]; then t=$(date +%s); return 0; fi  # juego aún no desplegado/sin log
  console_locked && { t=$(date +%s); return 0; }
  s=$(sig)
  if [ "$s" != "$last" ]; then last=$s; t=$(date +%s); restarts=0; return 0; fi
  wait=$(( STALL * (1 << (restarts>4?4:restarts)) ))  # backoff exponencial hasta 16x
  if [ $(( $(date +%s) - t )) -ge $wait ]; then
    restarts=$((restarts+1)); restart "log quieto ${STALL}s, #$restarts"
    console_locked && { t=$(date +%s); return 0; }
    # copia del último log para diagnóstico
    timeout 30 curl -s "${F}${LOGDIR}/hotwheels.log" -o "$OUT/hang-$(date +%H%M%S).log"
    last=$(sig); t=$(date +%s)
  fi
}
watchdog_main(){ watchdog_startup; while true; do watchdog_cycle; done; }
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then watchdog_main; fi
