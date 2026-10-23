# BUG-047 — pata Vita3K/GXP (sol-gfx)

Acuerdo previo con opus-gfx: `coordination/opus-gfx.md`, sección «Pata GXP». Orquestador asignó GXP a sol-gfx y qemu/RefRaster + Wine a opus-gfx. **No sustituye** la matriz final del oráculo HW-067 ni evidencia de consola.

Baseline del candidato C1: VPK SHA256 `fab6a3bd6204d18502daad7ed4428952066f4396a1421512358caef095a5a6c6`, 1.936.046 B. Código renderer `9da4281`, build aislado `out/sol-gfx-vita`. Config explícita `fps_mode=original`, `scaling=fit`, `render_scale=native`, `filter=game`; pipeline y caché default. `env.txt`: sólo `HW_IMAGE_TRACE=1` y `HW_INPUT_SCRIPT=ux0:data/hotwheels/sol-gfx-race.script`. Sin AOT override. Guardar SHA del guion y settings usados; tres candidatos settings (mayúscula/minúscula/overlay save) reciben bytes idénticos durante corrida. Al terminar se restauran byte a byte env/ini/settings y se borra el script temporal; lock VITA3K_BUSY protegido con creación exclusiva.

Una corrida con argumento 65 s por pista, capturas entre sleeps de 1 s (el trabajo de captura agrega tiempo de pared); retener un PNG representativo por pista en `out/bug047-gxp-evidence/track-N.png` (gitignored). Comparación estructural de carrera quieta/grilla, no mismo número de frame ni equivalencia píxel exacta (timer/RNG). Una selección no verificada invalida esa fila. PASS del runner sólo acredita boot/ejecución del contenedor, no ausencia de glitches.

| Pista solicitada | Identidad verificada | Guion/settings | Momento screenshot | D3D / FPS medidos | Visual | Estado |
|---|---|---|---|---|---|---|
| 1 / WAVE (control playa) | sceIoOpen data/wave/wave.sfp; grilla de arena | race_enter.script / race-d3d-settings.dat | shot_016, HUD 00:00:00 | target 800×600; guest 30.297–31.947 Hz / consumer 3.256–3.725 Hz (últimas 3 ventanas) | grilla, bandera, autos, pista/texturas coherentes; sin glitch evidente en muestra | runner PASS; comparación estructural manual WAVE vs Wine PASS |
| 2 / OFFICE (orden menú confirmado) | sceIoOpen data/office/office.sfp; escritorio | race_enter.script / race-d3d-track2.dat | shot_015, HUD 00:00:00 | target 800×600; guest 38.392–38.507 Hz / consumer 2.664–2.722 Hz (últimas 3 ventanas) | escritorio, micrófono, teléfono/cable y grilla coherentes; fin HUD 01:08:44 sin corrupción evidente | runner PASS; comparación estructural manual vs Wine PASS |
| 3 / SLUMBER | sceIoOpen data/slumber/slumber.sfp | race_enter.script / race-d3d-track3.dat | shot_035, HUD 00:28:30 | guest 39.474–40.003 Hz / consumer 5.098–8.317 Hz (últimas 3 ventanas) | escenario dormitorio, elefantes/caja/cartón/autos coherentes | runner PASS; wrapper recibió SIGTERM después; config recuperada y verificada |

Hashes completos, configuración y verificación de restauración en `out/bug047-gxp-evidence/track-N-meta.json`; fragmentos de logs en la misma carpeta. Sólo se conserva corrida completa si falla, según política de disco.

## Control WAVE (2026-10-21, Vita3K)
Guion SHA256 `fefc5a8374f92ac0af4cee35dceaffa135a2ffad057885b7f3f2a4e1258d9860`; settings SHA256 `0e51d8d195b712441eb4c94fd8a053c50ca123b752517cd5207d2cd7cdce4218`. Capture `out/bug047-gxp-evidence/track-1.png` (960×544, 450.139 B, crop exacto del framebuffer; sin reescalado), de shot_016/grilla HUD 00:00:00. Identidad por sceIoOpen `ux0:/data/hotwheels/data/wave/wave.sfp`. Log compacto `track-1-summary.log`.

Últimas ventanas pipeline: guest/consumer Hz 30.297/3.256 (132/1530 completados/submits, 1397 drops acumulados), 31.947/3.594 (168/1850, 1681 drops), 31.176/3.725 (206/2168, 1961 drops). Tasa calculada por la telemetría en sus ventanas de ~10 s, no por screenshots. Tres ventanas sin uploads backend; carry conserva bindings de texturas en los drops. Estos FPS del emulador no son FPS de consola.

Limitación de config: en esta primera corrida `fps_mode=original` quedó bajo `[general]`; parser lo ignoró y cayó al default Original, confirmado por `fps_mode guardado=original efectivo=original` en log. Se corrigió sección `[display]` en receta para corridas siguientes. Config/archivos previos restaurados byte a byte y lock liberado; corrida completa pasada borrada tras guardar un PNG+resumen+metadatos.

Comparación manual de control con la referencia Wine ya generada por opus (`out/wine-track1/shot_0160.png`): mismo trazado/grilla, vallado izquierdo, piedras y postes de bandera, vehículos y HUD coherentes. Cambian color/orden de AI, escala y fase del countdown; no hay triángulos desbocados o texturas sustituidas evidentes en esta pareja. Nivel emulador+Wine, inspección visual estructural; sin umbral numérico/pixel exacto y sin afirmar cobertura temporal completa.

Receta HW-067 verificada posteriormente: menú Wave/Office/Slumber (los índices 1/2 se intercambian antes del array de archivos). `race-d3d-trackN.dat` fuerza carrera simple (línea 5=1), índice N−1 (línea 6), pistas desbloqueadas (línea 18=2). Reemplaza guiones track2/3 que no cambiaban pista. Tabla corregida con ese mapeo; la identidad de cada fila se verifica de nuevo en GXP.

## OFFICE (track2.dat, Vita3K)
Settings SHA256 `3bea107cfd228fb2e484fa325b858f18bb50419e26afaa8ed21f0c0c4d4db90d`; guion/VPK iguales al control. `sceIoOpen ux0:/data/hotwheels/data/office/office.sfp` confirma identidad. `track-2.png` (960×544, 258.105 B), shot_015/grilla HUD 00:00:00; log compacto y metadatos track-2*.

Últimas ventanas pipeline guest/consumer Hz: 38.452/2.682 (189/1859 completos/submits, 1669 drops), 38.507/2.722 (217/2255, 2037 drops), 38.392/2.664 (244/2644, 2399 drops). Cero uploads backend en esas tres ventanas. GXP y Wine de opus (`out/wine-track2/shot_0160.png`) coinciden estructuralmente: madera, micrófono izquierdo, cable/postes/bandera y objeto rojo derecho; varía un auto AI y el tamaño de salida. Revisé además shot_065/HUD 01:08:44: escritorio/cable/texturas/autos coherentes. **No reproduce BUG-047 en estas muestras**; no es prueba continua de cada frame.

Config/restauración por hashes idénticos antes/después en JSON; runner PASS, 66 capturas antes de reducir a una imagen y log compacto. Corrida completa pasada borrada.

## SLUMBER (track3.dat, Vita3K)
Settings SHA256 `8a645bdf5cf75a6ded0b1e88ff6b73ca4d07636decfeb4b429e91c14c0ec127a`; identidad `sceIoOpen data/slumber/slumber.sfp`. Un PNG en `out/bug047-gxp-evidence/track-3.png`, 960×544, 292.867 B, shot_035 HUD00:28:30; también inspeccioné shot_016 y shot_045/HUD00:44:08. Sin corrupción geométrica/texturas evidente en esas muestras. No se cotejó aún contra Wine Slumber.

Últimas ventanas guest/consumer Hz 40.003/5.098 (278/2384 completos/submits, 2105 drops), 39.474/5.526 (334/2784, 2449 drops), 39.902/8.317 (418/3187, 2768 drops). Runner final PASS, 66 capturas. El wrapper Python recibió SIGTERM/143 **después** de imprimir PASS y antes de ejecutar finally; el backup de esa corrida sólo vivía en memoria. Recuperé los bytes originales conocidos y validé sus hashes contra backup de track2 (idéntico estado inicial), incluyendo settings overlay CRLF sin newline final. Metadatos track-3-meta.json registran la recuperación. Lock liberado; corrida completa pasada borrada luego de extraer evidencia mínima.

## Alcance pendiente tras cambio de foco del orquestador
Los tres runs usaron `race_enter.script` (Return; sin acelerador explícito). Hay muestras de carrera, pero **no satisfacen todavía conducción deliberada/cámara/LOD durante 20–40 s**. Repetir con `tools/race_full.script` (Up desde frame600 y steering alternado) junto al VPK diagnóstico SPEC-052. El diagnóstico aún no está implementado: sólo existe contrato SPEC-052 y anexo de diseño `052-vpk-diagnostico-implementacion.md`. Fase 2 en consola sigue pendiente; no cerrar BUG-047 por estas muestras.
