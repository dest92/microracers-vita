# HW-061 — progress.txt por corrida Vita3K

## Problema

El runner Vita3K conserva el `audio.log` de cada corrida en su artefacto, pero `progress.txt` usa append y contiene diagnóstico de loader, heap, PMU y fallos. En el home compartido, las trazas viejas se mezclan con corridas nuevas. Además el runner fija el perfil compartido, lo que dificulta probar captura sobre un perfil aislado.

## Contrato observable

- Cada salida de `vita3k_run.sh` contiene `progress.txt` con sólo los bytes agregados durante esa corrida, usando el mismo snapshot robusto de HW-060.
- `VITA3K_HOME=/ruta` selecciona explícitamente otro home Vita3K; si no se indica, se conserva `shared/Vita3K`.
- El home se monta tal cual; el runner no copia firmware ni borra datos.
- Captura ocurre tras el final, timeout o fallo de arranque de Docker; el contenido persistente no se trunca ni modifica.

## Criterios verificables

1. Test de runner con perfil temporal y Docker simulado verifica que `progress.txt` y `audio.log` contienen sólo escrituras simuladas durante `docker run`.
2. `bash -n vita3k_run.sh` y la suite HW-060 pasan.
3. Smoke Vita3K PASS crea artefacto `progress.txt` idéntico al delta de fuente y no cambia el prefijo anterior.

## Razonamiento de sistemas

`wx86_progress` abre `progress.txt` en append. El runner toma snapshot antes de lanzar el proceso y extrae luego de que el contenedor sale; el digest/inode de HW-060 distingue append de reemplazo sin alterar el orden de E/S guest. El home configurable permite probar con estado pequeño y aislado, en vez de arriesgar los datos del perfil compartido.

## No objetivos

- Copiar o instalar el firmware Vita3K, clonar el home ni permitir corridas concurrentes en un mismo home.
- Cambiar contenido o frecuencia de las escrituras de `progress.txt`.
