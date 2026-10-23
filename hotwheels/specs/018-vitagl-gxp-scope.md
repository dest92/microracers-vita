# HW-011 — vitaGL integrado y alcance GXP

## Decisión y problema

Vita3K no implementa los imports ShaccCg que el sample standalone necesita para compilar shaders FFP. El orquestador decidió que el renderer D3D6 del juego no dependa del FFP de vitaGL: usa GXP propios precompilados, enlazados con vitaGL. Esto mitiga BUG-015 para el juego, pero no demuestra que el sample FFP funcione.

## Alcance revisado

HW-011 cubre la integración/build de vitaGL y las rutas GXP que consume el juego (present P8 y renderer D3D6). La ejecución visible del sample FFP standalone deja de ser criterio de bloqueo para el juego; su limitación en Vita3K queda documentada como pendiente independiente en BUG-015. La compilación/link del sample puede servir para verificar toolchain, no para afirmar que FFP se presenta.

## Criterios observables

- Build Vita incorpora vitaGL y enlaza los blobs GXP propios.
- El present P8 sintético se ve en Vita3K (HW-021); la prueba de convivencia con renderer D3D6 y carrera en Vita3K cumple HW-029.
- Ningún camino GXP integrado invoca compilación FFP/ShaccCg en runtime.
- BUG-015 queda mitigado para el juego; el sample standalone sigue reportado como no funcional hasta soporte ShaccCg en Vita3K.

## Evidencia / no objetivos

HW-021 valida P8 y HW-029 valida carrera D3D6. Esta spec no atribuye esas evidencias al sample FFP standalone ni declara que Vita3K implemente ShaccCg. Requiere revisión de diseño antes de cerrar HW-011 bajo el alcance revisado.
