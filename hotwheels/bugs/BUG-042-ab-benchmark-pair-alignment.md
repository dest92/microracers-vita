# BUG-042 — el resumen A/B puede mezclar iteraciones

- Nivel: **estático**, 2026-10-04. Owner: **reviewer**. Severidad: P2 para validez de mediciones.
- Estado: **cerrado** (2d52a8b test-first; 1/1 PASS; baseline previo reproduce delta incorrecto; review estática `coordination/re-review-2b09fca-bugs.md`).

## Problema

`tools/ab_cpu_frame.sh` omitía del array A o B cada pata que terminaba con `ERROR`, pero luego calculaba el delta pareado por índice entre arrays compactados. Si fallaba A en la iteración 1 y B pasaba, el B de esa iteración podía compararse con A de otra iteración; la mediana pareada ya no representaba patas consecutivas.

## Fix

El loop ahora retiene resultados en `leg_a`/`leg_b` por iteración y sólo agrega ambos a los arrays si los dos pasaron. Así las patas emparejadas conservan el mismo índice y una falla invalida únicamente ese par.

## Verificación

`tests/test_ab_cpu_frame_pairing.py` simula A1 fallida, B1 exitosa y A2/B2 exitosas. Con el script corregido informa sólo el último par (+50,0 %); contra el padre de `d7a1ee5` el test falla como esperado: arrays mezclados daban +100,0 % pareado y +75,0 % en medias. QEMU no se ejecuta. Review estática del fix APRUEBA en `coordination/re-review-2b09fca-bugs.md`.
