# BUG-028 — LogBuffer Vita recibe escrituras concurrentes sin serialización

- Nivel: **estático**, 2026-10-03. Owner: **runtime**. Severidad: mayor para integridad de logs/mediciones.
- Estado: **cerrado** (`f420465`; regresión TSan y pruebas I/O PASS; re-review APRUEBA).

## Evidencia
- `runtime_vita.cpp:378` crea `present_thread`; dentro del loop `runtime_vita.cpp:429` escribe las ventanas FPS con `LOG()`.
- En paralelo, `ns->run()` ejecuta el guest en el hilo llamador (`runtime_vita.cpp:505`); su callback D3D escribe desde el camino de escena/present (`runtime_vita.cpp:261–280`). Ambos pueden alcanzar el `LOG()` global.
- `LOG()` invoca `lb.write()` directamente (`runtime_vita.cpp:99`). `LogBuffer::write/flush` modifica `buf_`, `sync_left_`, `last_ms_` y `flushes_` sin mutex ni atomics (`io/log_buffer.cpp:13–26`, `io/log_buffer.h:28`). La clase dice requerir un solo hilo o lock del llamador, pero estos dos callsites no comparten uno.

## Riesgo confirmado (antes del fix)
En Vita, dos cores pueden entrar simultáneamente: eso es data race C++ sobre `std::string` y el contador `sync_left_`, no sólo una carrera de orden de archivo. Puede perder/corromper líneas o estado del buffer precisamente durante las ventanas de FPS. `90a4227` hace síncronas las primeras 256 escrituras pero no las serializa.
El fix debe serializar dentro del logger **todas** las llamadas/flushes y comprobar que el sink no reentre. Cada flush Vita abre/escribe/cierra el archivo; las primeras 256 escrituras pueden ser hasta 256 lotes de syscalls.

## Cierre — bugs (2026-10-03)
`LogBuffer` protege `write`, `flush` y `flushes()` con el mismo mutex, manteniendo el sink dentro de la sección crítica. El sink actual sólo hace `sceIoOpen/Write/Close` y no reentra. Test-first `be4948e` reprodujo carrera TSan en `std::string::append` (rc 66); fix `f420465` pasa TSan (4 productores/4000 registros), `run_io.sh` y compilación cruzada Vita de `log_buffer.cpp`. Re-review `coordination/review-BUG-028-bugs.md` APRUEBA.

El P2 de propagación del fallback cuando TSan no inicia quedó resuelto por `1598fcb`, `009f127` y `87e0f51`: el fallback hace stress sin sanitizer, y `run_all` conserva la razón en su resumen. Re-review `coordination/re-review-f420465-bugs.md` APRUEBA; confirmó que data race real (rc66) y SIGSEGV después de iniciar `main` fallan, mientras errores de runtime antes de `main` activan fallback. La prueba no llama a `flush()` explícito en paralelo, pero ese método toma el mismo mutex.
