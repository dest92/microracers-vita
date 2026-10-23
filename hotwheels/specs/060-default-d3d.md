# SPEC-060 — D3D por defecto en instalaciones existentes
- Feature: HW-070 · Milestone: M8 · Dueño: fix-luna · Estado: borrador
## Problema
El segundo entero de `DATA/MENU/settings.dat` desactiva aceleración 3D cuando vale 0. El juego persiste esa preferencia en el save, que tiene precedencia sobre los datos staged; por eso corregir sólo `stage_data.sh` no cambia instalaciones existentes.
## Contrato observable
Al iniciar, si `[display] force_d3d=1` (default), el port actualiza únicamente el primer entero del settings efectivo a `1` antes de que el guest lo lea. El resto de bytes/líneas se conserva. `force_d3d=0` deja el archivo intacto. La preferencia del juego para software no se conserva mientras el override esté activo.
## Criterios de aceptación
- [ ] CA1: default de config es true y parser/serializador acepta ambos valores → `test_io`.
- [ ] CA2: sólo primer entero; LF/CRLF, whitespace y sin EOL; rechaza basura trailing, vacío usa default documentado y save/base con casing arbitrario resuelven vía VFS → `test_io`.
- [ ] CA3: override se aplica al settings save existente antes de guest y no actúa apagado → `test_io` sobre el helper de inicio.
## Diseño (bajo nivel)
El guest corre en el hilo principal y el setup ocurre antes de lanzar sus hilos; el archivo no tiene escritores concurrentes en ese punto. VFS resuelve el save existente antes de DATA y conserva el casing real de los directorios; al crear overlay nuevo se usa `resolve_write`. El contenido se limita a 4096 B, se transforma en memoria y se escribe a un temporal hermano antes de `rename`, evitando truncar el save ante salida abrupta. El primer entero de un archivo ausente/vacío se inicializa a 1 y la línea de resolución a 2 (800×600, default del port). No hay interacción CPU/GPU ni barreras ARM: el orden de inicialización precede la primera lectura guest. No se crea hilo ni syscall por frame. El override es persistente y por ello impide volver a software hasta desactivarlo en hwvita.ini.
## No-objetivos
No cambiar resolución (línea 2), otros settings, stage/deploy ni modo 3D si el usuario opta por `force_d3d=0`.
## Riesgos / preguntas abiertas
Settings no ASCII o sin entero inicial se dejan sin cambios y se informa en log; el fallback de stage sigue resolviendo la instalación nueva.
## Evidencia
Host para parser y transformación; estático para el orden de inicialización; consola no autorizada en este brief.
