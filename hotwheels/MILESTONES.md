# Milestones — Hot Wheels Micro Racers → PS Vita

**Requisitos duros del usuario (definición de 'port terminado'):** (1) **60 fps** en consola real durante la carrera; (2) **jugable completo con los botones físicos** de la Vita (sin touch/teclado obligatorios); (3) VPK instalable con guía; (4) **modo elegible por el usuario**: fps (original 40 / 60 interpolado / 60 nativo si aplica) y esquema de controles, en hwvita.ini + selector al arrancar (botones físicos).

| M | nombre | criterio de salida (verificable) | features | estado |
|---|---|---|---|---|
| M0 | Conocimiento | triage, referencia Wine, inventario winx86 + gap list, Vita3K operativo, research | HW-001..004 | casi: 001,002,003,005 done; 004 y 008 con cambios menores |
| M1 | Carga | hotwheels-vita carga el PE, resuelve imports (stubs que loguean) y llega al entry point (test host + Vita3K) | HW-005 | en curso (HW-005 done, HW-009 review, HW-014 harness) |
| M2 | Arranque | corre CRT init y WinMain hasta crear ventana/dispositivo gráfico sin crash | HW-006 | todo |
| M3 | Primer frame | intro/menú visible en Vita3K vía camino DDraw 8bpp por software (paleta → textura → pantalla); D3D6 no requerido | HW-006, HW-007 | **LOGRADO** qemu + Vita3K (menú real, >1400 frames, 2026-10-03 03:33; audio off por BUG-008) |
| M4 | Menú jugable | menú navegable con controles Vita, audio | — | **jugado por el usuario en consola real con botones** (audio distorsionado BUG-031) |
| M5 | Carrera | se puede correr una carrera completa | — | **carrera D3D6 visible en Vita3K** (GXP propios, 40 s sin crash, shot_039 2026-10-03 04:20); **corre en consola real** (usuario, entra a carrera vía guion) |
| M6 | Pulido | **60 fps en carrera (consola)**, controles físicos completos, vpk instalable, guía de instalación | — | todo |
| M7 | Mejora continua | caché de traducción persistente, interp60, dxlib reutilizable (ver feature_list M7) | HW-057, HW-059, HW-066 | en curso |
| M8 | Carrera jugable con calidad | (1) carrera nítida en consola: render ≥800×600 efectivo, captura + OK del usuario (BUG-032); (2) audio limpio: oído ≥60 s en consola, 0–1 underruns, sin distorsión (BUG-031); (3) **todas las pistas** sin glitches: diff vs referencia Wine/RefRaster bajo umbral por pista + OK del usuario en consola (BUG-047) | HW-067..071 | en curso (2026-10-21) |
