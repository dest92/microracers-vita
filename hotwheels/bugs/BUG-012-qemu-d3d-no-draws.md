# BUG-012 — qemu crea el device D3D de carrera, pero no llega a dibujar

Nivel: **qemu-arm** (integ/render, 2026-10-03). Dueño de diagnóstico/fix: **render** para el ring D3D; integ valida el recorrido del juego.
Severidad: **mayor** (falso bloqueo por evidencia incompleta). Estado: cerrado (2026-10-03).

## Evidencia

- `out/run-20261003-015157/stdout.txt`: CreateDevice de sonda 640×480 y carrera 800×600 devuelven `S_OK`; escenas D3D 0/1/2 tienen `draws=0`; luego EIP muestreado `0c300770`. Sin golden RefRaster de la escena real.
- `out/run-20261003-020323/stdout.txt`: ambos CreateDevice `S_OK`; sólo se observan las primeras 3 escenas vacías, con 7/1/1 palabras; `RefRaster::write_png` falla inicialmente desde frame 223. Esta muestra no cubría la carga/entrada posterior a la carrera.
- `out/run-20261003-022752/stdout.txt` con traza temporal: 39 `BeginScene`, 39 `EndScene`, 39 frames D3D cerrados; 8 logs de `DrawPrimitive` (límite de traza), todos `prim=5`, `FVF=0x104`, 3–4 vértices y `S_OK`. `frame_00268.png` muestra la carrera.
- Repetición sin instrumentación `out/run-20261003-023119`: frames D3D `frame_00230.png`–`frame_00269.png`; `frame_00269.png` inspeccionada, carrera visible 800×600. Fin controlado `frames=270`, `missing=0`.
- Contraste: Vita3K `build-vita3k/hotwheels-vita-20261003-015009` llega a carrera visible, con `D3D escena=100 draws=735` y frame 300 en vitaGL. La comparación MAE 2.98/255 corresponde al fixture sintético, no al juego.

## Conclusión

No había fallo persistente de `DrawPrimitive`: los reportes previos contaban sólo las tres primeras escenas, antes de terminar la carga de carrera. La validación host sí produce draws y PNGs RefRaster reales; no se necesitó fix de renderer.
