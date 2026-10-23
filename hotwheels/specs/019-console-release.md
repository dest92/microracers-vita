# HW-032 — perfil release y stage seguro a consola

## Problema

El VPK optimizado debe evitar I/O de log frecuente y aprovechar el scheduler/clock de Vita, sin asumir que el hilo servidor de otro juego existe aquí. La transferencia de HW-032 sólo prepara `ux0:data/hotwheels`; no instala el título ni lo lanza.

## Contrato observable

- Build Vita de release usa `-O2` para runtime y winx86, produce VPK válido `HWMR00002` y reporta su tamaño local.
- Defaults release, sólo si el usuario no los reemplazó en `env.txt`: `WX86_LOGASYNC=1`, `WX86_NOCANCEL=1`, `WX86_WAITROUND=1`, `WX86_COEURS=222`, `WX86_COEUR_SERVEUR=0`. LOGASYNC conserva las líneas críticas síncronas; logs de runtime no se emiten por frame.
- El proceso solicita 444 MHz y registra el rc de la API. El guest runner permanece en USER_0 y el consumer de presentación interp60 se fija a USER_2 desde el propio hilo; el logger async de winx86 usa USER_1. No se activa CPU 3 ni se migra un server guest no identificado.
- `vita_deploy.sh --data-only` valida TITLEID, sube VPK y stage sólo bajo `ux0:data/hotwheels`, usa `.new` + verificación de tamaño + rename, no invoca VitaCompanion, no escribe `ux0:app` y no instala ni lanza.
- Ante una verificación final fallida, intenta restaurar los bytes previos o dejar el destino ausente si inicialmente no existía; elimina el temporal/resultado inválido. `SIZE` y `RNTO` son roundtrips FTP separados, así que el rollback también puede fallar. Si falla: no informa éxito, preserva `.previous` como recuperación cuando exista, e informa el error original, el error de rollback y las rutas remotas que requieren inspección/limpieza. Con destino inicialmente ausente, informa si no pudo borrar el resultado inválido.
- Dos invocaciones de deploy al mismo destino no deben compartir temporales/backs ni intercalarse. Serializar el deploy o usar una exclusión equivalente evita carreras en `.new`/`.previous`; renames individuales no protegen el read-upload-verify-rename compuesto.
- La suite fake verifica alcance, ausencia de comandos de consola y tamaños; el VPK y cada archivo transferido se reportan con tamaños locales/remotos iguales.

## Criterios de aceptación

1. Tests host cubren perfil/defaults sin pisar valores de usuario y deploy data-only sin escrituras a `ux0:app` ni llamadas a VitaCompanion.
2. `build_vita.sh` completa con `-O2`; inspección de comandos confirma el flag en runtime y winx86; VPK pasa `test_vpk.sh` y `test_vita_deploy.py`.
3. En consola, build release y stage-only terminan con tamaños VPK/archivos verificados; no instalar ni lanzar.
4. Registrar límites: 444 MHz es solicitud aceptada/rechazada por el sistema, no medida de clock sostenido; affinity sólo se afirma si rc es 0; `WX86_COEUR_SERVEUR=0` por evidencia estática de juego monohilo.
5. El fake FTP fuerza error de tamaño final con destino inicial ausente y existente, y fuerza fallas de limpieza/restauración; verifica error explícito y preservación del backup recuperable. Además comprueba que invocaciones concurrentes no colisionen o que el despliegue se serialice.

## Fuera de alcance

- Instalar/ejecutar el VPK o modificar `ux0:app`, plugins, config del sistema o datos ajenos.
- Afirmar FPS/performance en consola sin un A/B propio; los A/B D2Vita sustentan la selección inicial de knobs, no predicen Hot Wheels.
- Habilitar afinidad a USER_3 o mover guest server sin identificarlo y medirlo.
