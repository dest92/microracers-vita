# HW-049 — cleanup cuando falla SIZE del temporal

## Problema

Después de `STOR remote.new`, `Deploy.size(remote.new)` puede fallar por una interrupción FTP distinta de la respuesta `550` que `size()` interpreta como archivo ausente. La excepción actual deja el temporal remoto sin verificar y sin borrar.

## Contrato observable

- Si la consulta SIZE del temporal lanza error, o devuelve `None` tras el `STOR`, no se modifica el destino original ni se avanza a hash/rename.
- Se intenta borrar `remote.new` bajo el lock por destino existente.
- Si DELE funciona, se relanza la excepción original de SIZE.
- Si DELE falla, el error informa ambos fallos y la ruta remota pendiente.
- `None` es ambiguo (archivo ausente o respuesta SIZE no autorizada): se intenta DELE. Si DELE también falla, el temporal queda como limpieza no confirmada y se informa la ruta; no se presume que 550 demuestre ausencia.

## Criterios de aceptación

1. Fake FTP completa STOR y luego hace fallar SIZE con error temporal o respuesta 550: regresión roja antes del fix; luego el original permanece, no hay `.new` y se propaga/report el fallo de SIZE.
2. Si DELE también falla, se reportan causa SIZE, error de cleanup no confirmado y ruta, sin mutar el original.
3. `PYTHONDONTWRITEBYTECODE=1 python3 hotwheels/tests/test_vita_deploy.py` PASS y `git diff --check` PASS.

## Fuera de alcance

RETR/hash, FTP real, reintentos, consola y alcance de rutas.
