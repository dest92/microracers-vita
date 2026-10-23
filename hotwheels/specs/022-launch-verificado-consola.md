# Spec 022 — Lanzamiento verificado en consola (BUG-026)

## Problema
En consola real, `launch HWMR00002` de VitaCompanion responde `Launched.` aunque no
cree un proceso nuevo: si la instancia anterior quedó trabada en una llamada al
kernel (log quieto, sin coredump), `quit all` no la destruye y `launch` sólo la
reanuda. Medido 2026-10-03 (consola): con ese estado ni el eboot bueno
(24,353,406 B) ni el nuevo escribieron nada; tras `reboot` ambos arrancaron.
Además `vita_deploy.py` daba la app por viva si `hotwheels.log` tenía tamaño > 0,
y el log de la corrida anterior cumple eso (falso positivo).

Teoría: un proceso con un hilo bloqueado en un syscall no interrumpible (espera
de GPU/display, handler de excepción kubridge) no termina hasta que el kernel lo
libera; el shell trata la app como "viva" y el lanzamiento se vuelve un resume.
Ningún archivo viejo prueba arranque: hace falta una miga que sólo un proceso
recién creado puede escribir.

## Contrato
1. `runtime_vita` escribe `ux0:data/hotwheels/boot_probe.txt` de forma síncrona
   (sceIoOpen/Write/Close, sin heap): `ctor101 t=…` desde un constructor de
   prioridad 101 (trunca) y `main t=…` al entrar a `main` (append).
2. `Deploy.launch_verified()` borra `boot_probe.txt`, lanza y espera hasta
   `polls*interval` s a que contenga `main`. Si no aparece: `quit all`,
   `reboot`, espera a VitaCompanion (`version`), `nosleep on`, borra la sonda y
   relanza una sola vez. Devuelve True sólo si la sonda fresca contiene `main`.
3. `Deploy.run()` usa `launch_verified()`; un log viejo no cuenta como vida.

## Criterios de aceptación
- Test: log viejo no vacío + sin sonda ⇒ no se considera vivo y se pide `reboot`.
- Test: sonda con `main` tras el primer launch ⇒ sin reboot.
- Test: sonda sólo tras el reboot ⇒ True con un único reboot.
- Test: nunca aparece ⇒ False, un único reboot (sin bucles).
- Consola: eboot actual arranca tras reboot con `boot_probe.txt` fresco.

## No-objetivos
- Diagnosticar por qué la instancia previa se cuelga en menú (bug aparte).
- Cambiar el watchdog más allá de reusar la misma verificación.

## Enmienda (BUG-035, 2026-10-03)
El `reboot` automático deja la Vita en pantalla de bloqueo: la sonda aparece pero el sistema suspende la app a ~15 s. `launch_verified` exige además `hb.txt` con `t ≥ 20000`; si no, crea `out/CONSOLE_LOCKED` y devuelve False. El watchdog marca el flag tras reboot y no relanza.
