# BUG-035 — Instancia trabada que sobrevive a `quit all` (origen del zombi de BUG-026)

- Nivel: **consola real**, 2026-10-03. Dueño: opus. Estado: **abierto**.

## Evidencia
- El eboot bueno (24,353,406 B) corrió en menú hasta `present frame=6600` (`hotwheels.log` 7,530 B) y quedó quieto a las 12:11 (reloj consola); `audio.log` también se detuvo a las 12:11. Sin `psp2dmp`.
- A partir de ahí `quit all` + `launch` no creaba proceso (ni miga crt0) hasta `reboot` (BUG-026, spec 022).
- Contraste: una instancia quieta en `present frame=100` (menú inicial) sí muere con `quit all` (sonda fresca al relanzar).

## Hipótesis (refutables)
1. Un hilo quedó bloqueado en un syscall no interrumpible (espera de GPU `sceGxmFinish`/display, o dentro del handler de excepción kubridge), lo que impide que el proceso termine.
2. Interbloqueo entre hilo de audio y present (ambos logs se detuvieron juntos).

## Próximo paso
Reproducir dejando el menú correr >5 min con watchdog; al quedar quieto, leer `progress.txt`/`audio.log` y probar `quit all` + sonda antes de reiniciar.

## Investigación opus — 2026-10-03 09:45–10:55 ART (nivel: consola, 10 corridas)
Instrumentación: latidos por hilo (spec 023, `hb.txt`), canal UDP no bloqueante del vigía (`HW_HB_UDP=ip:puerto`, prioridad 64, fijado a USER_1), fd persistente (`hb_fd.txt`), `std::set_terminate`/`atexit` → `LAST`, wrapper `--wrap=dyn86_fault_handle` → `FAULT`, `HW_TRACE=*` + `HW_TRACE_UDP=1`.

**Hecho reproducible:** con el menú ocioso el proceso entero se detiene a **~14.3–14.8 s del launch** (en una corrida 11.8 s), en 10/10 corridas. Hasta el último latido todo está sano: interp60 a ~60 fps, audio cada grano, presents DDraw a 60/s, memoria de usuario plana en 73,728 KB, cdram 81,920 KB, phycont 18,432 KB.

**Descartado con evidencia (cada uno reprodujo el congelamiento a ~14.5 s):**
- FTP concurrente: corrida sin tocar FTP 60 s.
- Inanición/core monopolizado: el vigía a prioridad 64 fijado a USER_1 también se detiene.
- E/S trabada: hb.txt (open por línea), UDP no bloqueante y fd persistente se cortan en la misma línea, con los últimos rc OK.
- interp60: `fps_mode=original`. Audio: `HW_AUDIO=0`. Dibujo DDraw en GPU: `HW_DIAG_NOPRESENT=1`.
- Salida silenciosa: sin `LAST` (ni terminate ni exit) y `launch` sin `quit all` no crea proceso → el proceso sigue existiendo.
- Fallo de CPU no manejado: sin `FAULT` desde `dyn86_fault_handle`; sin `psp2dmp` nuevo.
- Llamada Win32 disparadora: la traza UDP muestra el guest en su bucle ocioso estable (`PeekMessageA` + 2×`timeGetTime` por frame) hasta el último instante (timeGetTime guest 0x311f ≈ 12.6 s); ninguna API distinta antes del corte.

**Estado tras el corte:** `quit all` responde pero no destruye el proceso; `launch` reanuda/no crea proceso; con el tiempo el FTP de la consola da `550 Could not allocate memory` (memoria del sistema retenida). Sólo `reboot` recupera.

**Hipótesis vigentes (orden de probabilidad):**
1. Suspensión del proceso por el sistema por un evento que no es CPU fault: p.ej. fallo del GPU (GXM) en el D3D/vitaGL compartido, o watchdog del shell sobre un hilo; requiere ver la pantalla (diálogo de error) — pedir al humano una foto/observación en la próxima ventana.
2. Algo temporizado del runtime/winx86 a ~12–15 s (crecimiento JIT anticipado, `vm-domain BRACKET`, cambio de política GIL) que deja a todo el proceso detenido en el kernel (kubridge). Bisección siguiente: `WX86_KUBRIDGE=0`, `WX86_JITPOOL_SEGS=1`, y desactivar el BRACKET de vm-domain.
3. Attract mode/demo del juego tras ~12.5 s de menú ocioso que no hace llamadas visibles; con input (script o físico) el juego no se congeló (integ: 2,400 presents/100 s; 12:11: 6,600 presents).

**Mitigación operativa ya vigente:** `vita_deploy.sh`/watchdog verifican `boot_probe.txt` y rebootean (spec 022). Para sesiones de medición, arrancar con `HW_INPUT_SCRIPT` y no dejar el menú ocioso.

## Hipótesis 1 refinada — pools vitaGL (opus, 11:10 ART; estático + medida de menú)
- Ninguna corrida de consola que sobrevivió cerró escenas D3D (`d3d_scenes_closed=0` en la de 6,600 presents y en la de integ con script de carrera); las llamadas D3D son COM (vtable), invisibles a `HW_TRACE`. El congelamiento a ~12.5 s de reloj guest encaja con el arranque D3D del attract/demo.
- Medido en menú (hb.txt): vitaGL RAM libre 1,485 KB y VRAM 3,675 KB con pools de 8/16 MiB, mientras el sistema tenía 72 MiB usuario y 80 MiB CDRAM libres. Agotar el pool al subir texturas D3D puede llevar a GXM un puntero inválido → fallo de GPU → el sistema suspende el proceso (sin CPU fault ni coredump; Vita3K no lo emula).
- Cambio (sin verificar en consola): pools 24 MiB RAM / 64 MiB CDRAM, knobs `HW_VGL_RAM_MB`/`HW_VGL_VRAM_MB` (acotados 4–48 / 8–96) para A/B sin recompilar; test `test_vgl_pools`.
- Verificación pendiente (ventana consola): menú ocioso 60 s con defaults nuevos vs `HW_VGL_RAM_MB=8 HW_VGL_VRAM_MB=16`, con `HW_HB_UDP`; criterio: sin congelamiento a ~14.5 s y `d3d_scenes_closed>0`.
- Vita3K (d300bf5, 30 s menú ocioso): PASS; D3D arranca solo en menú (escena=600, D3D frame=800 a los 30 s) → confirma que el menú ocioso entra en D3D (attract/demo). Pools 24/64 inician OK en Vita3K.

## A/B consola 11:20–11:45 ART (opus) — todas se congelan a ~14.5 s
| corrida | resultado |
|---|---|
| pools vitaGL 24/64 MiB (d300bf5) | congela t≈14.56 s → hipótesis pools REFUTADA |
| `WX86_KUBRIDGE=0` | congela t≈14.48 s |
| `HW_DIAG_NOD3D=1` (sin backend D3D) | congela t≈14.51 s |
| `race_enter.script` + `HW_AUDIO_SELF_PACED=1` (con entrada) | congela t≈14.52 s |
- Integ (HW-038, PMU+race) también: latido detenido a t=13.2 s, frame 100.
- **Nuevo eje:** antes del primer reboot de hoy (09:26 ART) había corridas largas (6,600 presents; carrera de 100 s de integ). Desde ese reboot, TODA corrida (incluido el eboot viejo 24,353,406 B: log 1,408 B en frame 100) se congela a ~13–14.5 s del launch, con o sin entrada/D3D/audio/interp/kubridge. `ur0:tai/config.txt` sin cambios desde 2026-09-25 (leído, no tocado); plugins de boot: kubridge, vitacompanion(_kernel), vpmu, fd_fix, storagemgr…; en SceShell: vitacompanion, vdb_daemon, shellbat, vflux.
- Hipótesis vigente: dependencia del ESTADO DE LA CONSOLA post-boot (no del binario ni de un componente del runtime). Próximos pasos: (a) observación humana de pantalla en el instante del congelamiento (¿diálogo/notificación del sistema? ¿vdb_daemon?); (b) eboot mínimo vitaGL sin guest (sólo clear+swap 60 s) para ver si cualquier app propia se congela a ~14.5 s; (c) comparar con D2Vita en el mismo estado.
- Experimento (b) listo: `hotwheels/tools/diag_minvgl/build.sh` → `build-diag-minvgl/eboot.bin` (6.4 MB; vitaGL clear+swap 60 s, latido UDP a 192.0.2.20:9099 + `ux0:data/hotwheels/minvgl.txt` cada 0.5 s, sin guest/winx86). Procedimiento: CONSOLE_BUSY, reboot, renombrar eboot.bin→eboot.hw, subir minvgl como eboot.bin (verificar sha256), launch, mirar si `minvgl t=` pasa de 15000 y llega a `fin 60s`; restaurar eboot.hw→eboot.bin.

## Aporte render — memoria vitaGL durante carrera Vita3K (2026-10-03)
- Corrida aislada de 60 s con VPK `SHA256 b2a2f0bd…`, `settings.dat=1/2`, `fps_mode=original`, `render_scale=native`. `CreateDevice(800×600)` S_OK; primer present D3D frame 300, backend vitaGL; escena 100=669 draws y escena 200=956 draws. Sin freeze/crash en 60 s (Vita3K, no consola).
- Pools inicializados: RAM 24.576 KiB, CDRAM 65.536 KiB. Heartbeat del emulador reportó memoria sistema constante `user=91.136 KiB`, `cdram=49.152 KiB`, `phy=18.432 KiB`; `vglMemFree` informó ram/vram 5 KiB a t=5,2 s, aumentando hasta 68 KiB a t=70,3 s (14 muestras; ambos iguales). No hay tendencia descendente en esta muestra, pero esos números no concuerdan con la capacidad de pool ni con la lectura Vita real de 1.485/3.675 KiB; Vita3K no sirve para estimar agotamiento de pools en consola. No refuta ni confirma BUG-035 en Vita.

## CAUSA (opus, 11:40 ART; nivel consola) — pantalla de bloqueo tras reboot
- `diag_minvgl` (app mínima vitaGL, sin guest/winx86) se detuvo a t=14,752 ms; la variante **sin GPU** (sólo latido + main durmiendo) a t=14,328 ms. Cualquier app propia lanzada por VitaCompanion se suspende a ~15 s → no es Hot Wheels, ni winx86, ni vitaGL.
- Coincide con el corte temporal: antes del primer reboot del día (09:26 ART, consola desbloqueada por el usuario) hubo corridas de minutos; desde ese reboot (y los del watchdog/deploy de spec 022) todas mueren a ~13–15 s. Tras boot la Vita queda en la pantalla de bloqueo; la app lanzada por VitaCompanion no llega a primer plano y el sistema la suspende al terminar la gracia de arranque. La instancia suspendida retiene memoria (FTP `550`) y `launch` la "reanuda" sin proceso nuevo.
- Nivel de evidencia: consola, 3 binarios distintos (HW, minvgl, minvgl-nogl). Falta confirmación visual humana de la pantalla de bloqueo (refutable: si tras desbloquear a mano minvgl-nogl también muere a 15 s, la hipótesis cae).
- Fix operativo: `vita_deploy.launch_verified` exige `hb.txt` ≥ 20 s además de la sonda y marca `out/CONSOLE_LOCKED` en vez de reportar éxito; el watchdog tras cualquier reboot marca `CONSOLE_LOCKED` y deja de relanzar; AGENTS.md: no rebootear con el flag. Consola marcada LOCKED ahora.
- El zombi original de las 12:11 (antes de cualquier reboot, tras ~6,600 presents) sigue sin causa conocida: queda abierto con esta instrumentación lista para cuando la consola esté desbloqueada.
