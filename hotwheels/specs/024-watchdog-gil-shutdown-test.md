# SPEC-024 — contrato ejecutable del watchdog GIL

## Problema

La regresión de BUG-024 inspecciona `host_main.cpp` con regex. No ejercita el mutex real ni detecta si una contención transitoria se convierte en aborto o si `request_shutdown()` corre sin ownership.

## Contrato observable

- El watchdog intenta tomar el GIL sin bloquear de manera indefinida.
- Llama `request_shutdown()` exactamente una vez sólo cuando posee el GIL y siempre lo libera.
- Si el mutex está ocupado por toda la ventana configurada, agota los intentos y no invoca el método.
- Si el mutex se libera durante los reintentos, la petición se emite y el test puede reacquirir el mutex al retorno.

## Criterios verificables

Prueba host enlazada al GIL real: adquisición inmediata, contención liberada durante el retry y contención permanente hasta agotar intentos. `run_all.sh` debe ejecutar esos casos por CTest.

## Implementación y evidencia

Test-first `48a5957` falló al compilar porque faltaba `host/watchdog_shutdown.h`. `bf4beea` extrajo el retry a helper inline y conectó watchdog/test. CTest dirigido: 1/1 PASS (3 casos internos); `run_all.sh`: 25 PASS, 0 FAIL, 1 integración qemu omitida por diseño. `run_host.sh 3`: límite real, `shutdown requested`, 4 frames, missing=0, rc0. Review independiente APRUEBA: `coordination/review-HW-037.md`.

## No objetivos

No cambiar scheduler, política de forced exit ni estado del renderer. El helper debe permanecer fuera del hot path; su único caller es el watchdog.
