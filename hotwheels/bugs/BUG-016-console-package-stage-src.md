# BUG-016 — el paquete de consola no indicaba el origen de los datos

Estado: **cerrado**. Evidencia: test host con VPK sintético y fixture propio, sin payload del juego.

## Síntoma

`package_console.sh` copiaba `stage_data.sh` a la raíz del paquete, pero MANIFEST y checklist
indicaban ejecutarlo sin `SRC`. Fuera del repositorio, el default `$HW/extracted/original/Program_Files`
no existe, por lo que el script fallaba. INSTALL mencionaba `SRC`, pero con otro comando/path.

## Corrección y regresión

MANIFEST, INSTALL y checklist ahora comparten el comando portable
`SRC=/path/to/Program_Files ./stage_data.sh /path/to/mount/ux0/data/hotwheels`.
CA6 extrae el paquete en una ruta temporal y ejecuta `stage_data.sh` con un fixture inventado;
CA7 comprueba que los tres textos contienen el comando portable. Sin `SRC` el script falla antes
de crear el destino. `bash tests/test_console_package.sh` PASS.
