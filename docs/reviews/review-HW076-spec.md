# Review HW076 — SPEC-065, tests y CA3

Fecha: 2026-10-23. Review de diseño/fixtures y paquete local; sin instalación, Vita3K ni consola.

## Dictamen

**CAMBIOS (cobertura de pruebas, prioridad baja).** La implementación observada cumple el flujo central de SPEC-065 y el CA3 es íntegro: la prueba de paquete no altera el eboot base y contiene exactamente las seis entradas esperadas. No encontré una falla funcional en el generador ni en `package.sh`. Para cerrar CA1/CA2 como evidencia fuerte faltan fixtures que detecten recorte/deformación real y que inspeccionen las dos imágenes de portada dentro del VPK.

## Evidencia reproducida

- `python3 hotwheels/tests/test_sce_sys.py`: PASS, 4 tests.
- `bash hotwheels/tests/test_vpk.sh`: PASS, seis entradas y `TITLE_ID=HWMR00002`.
- CA3 `prepackage-manifest.json` y `manifest.json`: SHA-256 del premanifest `7dda48b7…e654e8ca`; manifiesto declara entradas verificadas y eboot idéntico. Verifiqué los hashes de las seis entradas ZIP contra el manifiesto; todos coinciden.
- VPK nuevo: 2.109.022 bytes, SHA-256 `c5b8898492abd84281ea35a89717c76e5042b47263da51db85ab1f93f952fbcc`, igual al manifiesto. VPK base e6749fb: SHA-256 `cdf84066a694bc3da9b2065cd2b82f15e6b007a836866e1ba38ab1691b6fd447`.
- `eboot.bin` del paquete nuevo y el del VPK base: ambos 1.967.903 bytes, SHA-256 `d6b0d94f192e09c5077d7c4ba10ba3b7ffd1604f326246dca00da3b2be689ade`; comparé los bytes extraídos del ZIP y son iguales.
- Los dos BMP de entrada y las copias de scripts/spec/test coinciden con los SHA listados en el premanifest; el manifiesto de salida verifica que no cambiaron durante el empaquetado.

## Cambios pedidos

1. **CA1, fixture geométrico (baja):** `test_original_images_and_proportions` usa un icono rojo sólido y una portada azul sólida. Las aserciones de color central y bandas negras no distinguen contenido preservado de una imagen recortada o deformada. Usar una imagen sintética con marcadores distintos en las cuatro esquinas y bordes, de relación de aspecto que requiera letterbox; comprobar posiciones/colores esperados en bg y startup. Así el test falla ante crop y stretch.
2. **CA2, ZIP (baja):** el test de paquete original comprueba sólo el píxel central de `icon0.png`. Decodificar además `bg.png` y `startup.png` desde el VPK y comprobar sus marcadores/dimensiones, junto con icono, para demostrar que los tres outputs provienen de la fuente original.
3. **Reproducibilidad genérica (baja):** `tests/test_vpk.sh` compara el árbol versionado con una generación genérica, pero luego llama `package.sh` sin fijar `HW_ORIGINAL_ART_DIR`. Si hay fuentes locales completas, esa etapa empaqueta originales por autodetección aunque el test pretende cubrir el camino genérico; fijar `HW_ORIGINAL_ART_DIR=''` en esa invocación para que la rama probada sea determinista.

La implementación usa `ImageOps.contain`, centra en canvas negro y cuantiza a 256 colores P8; el empaquetador genera en `OUT/sce_sys` y sólo empaqueta eboot, SFO e imágenes/XML esperados. El CA3 reportado inspecciona PNG localmente y no afirma validación en dispositivo. No instalé ni ejecuté el VPK.

## Re-review final — 156d960

**APRUEBA 156d960 para congelar publicación local de CA3.** El commit sólo modifica `test_sce_sys.py` y `test_vpk.sh`; el generador, `package.sh`, manifiestos y VPK no cambiaron.

- El fixture 100×50 ahora pone marcadores distintos en esquinas y bordes. `assert_cover` verifica la escala completa, ubicación de marcas y letterbox para ambas relaciones de salida; las mismas aserciones se aplican a `bg.png` y `startup.png` extraídas del ZIP.
- El fixture de icono contiene 256 colores y verifica la expansión nearest 4× píxel por píxel. La cubierta verifica suavizado observable. En el VPK, CA2 comprueba tamaño y modo P de las tres imágenes y marcadores de icono/portadas.
- `test_vpk.sh` invoca `package.sh` con `HW_ORIGINAL_ART_DIR=''`, aislando explícitamente el caso genérico.
- Reproducción actual: `python3 hotwheels/tests/test_sce_sys.py` PASS (5 tests); `bash hotwheels/tests/test_vpk.sh` PASS (6 entradas, TITLE_ID correcto).
- CA3 revalidado sin regenerar ni modificar artefactos: hash VPK `c5b8898492abd84281ea35a89717c76e5042b47263da51db85ab1f93f952fbcc`; el premanifest y los seis hashes del ZIP coinciden con `manifest.json`; eboot continúa igual al del VPK base e6749fb.

Alcance del dictamen: tests host y paquete local preparado. No implica instalación ni observación de LiveArea en Vita/Vita3K; la SPEC no las requiere para CA3.
