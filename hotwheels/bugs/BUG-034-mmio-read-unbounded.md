# BUG-034 — `mmioRead` reserva según count del guest

- Severidad: mayor · Estado: cerrado · Dueño: reviewer · Nivel: test host + sintaxis Vita

## Síntoma

`integration/mmio_bridge.cpp` reserva `std::vector<uint8_t>(n)` por el `LONG` del guest antes de leer. Un count cercano a `INT32_MAX` puede fallar/OOM en Vita aunque el archivo tenga pocos bytes.

## Contrato de fix

Transferir por scratch acotado (máximo 4 KiB), preservar EOF/short read y retornar bytes transferidos; request grande sobre fuente corta debe usar O(1) de memoria host.

Regresión: `tests/run_mmio_transfer.sh` con fuente sintética de 10 bytes y request `INT32_MAX`, además de un caso multi-chunk/EOF.

## Resolución

`733dd2f` reemplaza la reserva guest-size por scratch fijo de 4096 bytes y copia
en chunks. El test sintético verifica solicitud `INT32_MAX` sobre fuente de 10
bytes, retorno exacto y EOF multi-chunk. Evidencia: `run_mmio_transfer.sh`,
`run_winmm.sh` y compilación Vita `-fsyntax-only` del bridge PASS.
