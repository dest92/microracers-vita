# HW-048 — cleanup remoto ante fallo de verificación SHA-256

## Problema

`vita_deploy.py` relee `remote.new` por FTP para comparar SHA-256 antes de tocar el original. Si `RETR` se corta después de entregar bloques parciales, la excepción sale sin borrar el temporal; el destino original sigue sano, pero `.new` queda en la Vita y contamina reintentos.

## Contrato observable

- `put()` no inicia rename/backup hasta terminar RETR y validar SHA-256.
- Si RETR o la comparación falla, intenta borrar `remote.new`; el destino original permanece intacto.
- Si la limpieza tiene éxito, se propaga el error original de RETR/hash.
- Si también falla la limpieza, se reportan ambos errores y la ruta `.new` que requiere inspección; nunca se informa éxito.
- La exclusión por destino existente sigue cubriendo upload, verificación, cleanup y commit/rollback.

## Criterios de aceptación

1. Fake FTP entrega un bloque parcial de RETR y luego lanza error: test rojo antes del fix; después, se propaga el error, el original queda igual y `.new` desaparece.
2. Fake FTP falla también al borrar `.new`: error reporta causa de RETR, fallo de cleanup y ruta remota; original intacto.
3. `PYTHONDONTWRITEBYTECODE=1 python3 hotwheels/tests/test_vita_deploy.py` PASS y `git diff --check` PASS.

## Fuera de alcance

FTP real, consola Vita, retry automático, cambios en el protocolo de despliegue o al alcance de rutas.
