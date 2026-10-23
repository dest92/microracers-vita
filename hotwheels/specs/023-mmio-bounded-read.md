# BUG-034 — lectura acotada en el puente `mmioRead`

## Contrato

El tamaño `LONG` provisto por el guest no determina una reserva host del mismo tamaño. El bridge transfiere con scratch fijo de hasta 4 KiB, devuelve los bytes efectivamente leídos, corta en EOF o lectura parcial y propaga fallo de escritura guest. Un request `INT32_MAX` sobre un archivo corto consume memoria O(1) y no altera los bytes/cursor observables.

## Aceptación

- Testea request muy grande sobre fuente corta: máximo solicitado al reader y máximo entregado al writer ≤4 KiB; sólo se copian los bytes presentes.
- Testea request de varios bloques y lectura parcial/EOF.
- `run_mmio_transfer.sh` PASS y bridge Vita compila.

## Razonamiento

El guest comparte el proceso Vita, con memoria limitada. Reservar `n` antes de consultar disponibilidad convierte un argumento de API en presión de heap desproporcionada; chunking amortiza espacio a una cota fija, con costo O(bytes/4 KiB) de llamadas/iteraciones. La llamada corre dentro del shim bajo GIL, así que el buffer local pertenece al runner y no requiere lock adicional.
