# HW-060 — aislar audio.log por corrida Vita3K

## Problema

`vita3k_run.sh` reutiliza el home Vita3K y el juego abre `ux0:data/hotwheels/audio.log` en append. Capturas nuevas mezclan telemetría vieja; en HW-038 el archivo llegó a 87.882 B y hubo que aislar a mano la última instancia para medir el run.

## Contrato observable

- El runner guarda en el directorio de esa corrida `audio.log` con sólo los bytes agregados durante la ejecución.
- El archivo original de Vita3K permanece intacto.
- Si la fuente se reemplaza, se trunca (aunque luego crezca por encima del tamaño inicial) o no existía al tomar el snapshot, el artefacto contiene el archivo resultante completo. La detección compara device/inode y hash del prefijo previo.
- Si no se crea audio.log, el runner no falla por eso.

## Criterios verificables

1. Test de delta append: conserva el sufijo añadido desde el tamaño de inicio.
2. Test de truncado/reemplazo: conserva todo el contenido actual si el tamaño final es menor que el offset inicial.
3. `vita3k_run.sh` captura el delta tanto si el juego termina normalmente como si el contenedor termina con error/timeout o si falla `docker run` al iniciarlo.
4. Smoke Vita3K deja `audio.log` dentro del artefacto de corrida y la suma de bytes coincide con el delta fuente.

## Razonamiento de sistemas

`alog()` usa `fopen(..., "a")`, por lo que una corrida secuencial aporta un sufijo durable sin leer ni truncar datos previos. El offset se toma antes de arrancar Vita3K y se consume después de que el contenedor termina; así se evita modificar el orden de escrituras del guest y la captura no entra en el camino de tiempo real. La garantía aplica a ejecuciones secuenciales sobre el mismo home; no se promete separar dos Vita3K concurrentes que comparten ese home.

## No objetivos

- Rotar/borrar logs persistentes del home Vita3K.
- Aislar todos los archivos de perfil o soportar procesos concurrentes sobre el mismo VDIR.
- Cambiar el formato/ritmo de audio.log o tratar métricas Vita3K como medida de hardware.
