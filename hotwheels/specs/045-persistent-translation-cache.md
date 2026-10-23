# HW-059 — caché persistente de bloques traducidos

- Dueño: runtime
- Estado: en progreso
- Dependencia de perfil: HW-051

## Problema y contrato

El dynarec recompila los mismos bloques en cada proceso. `WX86_TCACHE=1` habilita una caché persistente optativa; si falta la variable, no se abre ni crea ningún archivo. La identidad incluye SHA-256 del PE, SHA-256 del ejecutable host que contiene traductor/helpers y target `qemu-arm-static`, combinados en `WX86_TCACHE_ID`. Una incompatibilidad, corrupción, cambio en los bytes guest o falta de elegibilidad es un miss seguro hacia `FillBlock` normal.

La implementación actual usa formato `WXTCARM3`, sólo Linux ARMv7 estático y de imagen fija bajo qemu-arm. Cada registro conserva el EIP, bytes guest exactos, cuerpo ARM normalizado, tabla de fixups tipados y `instsize`; CRC32 cubre el payload. No persiste `dynablock_t`, tail de enlace, estado x87 ni memoria guest mutable. El dispatcher ejecuta pass0 y compara todos los bytes fuente antes de reutilizar el cuerpo. Reconstruye por proceso el encabezado del bloque y el tail `jmpnext`, aplica fixups, copia `instsize` y hace `__clear_cache`.

Los fixups tipados actuales son ramas ARM B/BL locales (target como offset dentro del cuerpo) y `ADD Rd,Rn,#membase` (base host del arena codificada como ARM modified immediate; se reconstruye usando `dyn86_membase` del proceso que carga). Otros literales PC (incluidos VFP), referencias PC-relative externas, encodings desconocidos, ramas fuera del cuerpo y direcciones absolutas ambiguas causan miss. Los pares `MOVW/MOVT` se mantienen sólo si resuelven al ELF estático o a direcciones guest estables configuradas. La identidad de ejecución incluye y comprueba `WX86_CHAIN` (`WX86_TCACHE_CHAIN` debe coincidir); el hash exacto del ELF cubre helpers/datos y opciones de compilación. Los bloques `always_test` y formatos/targets no compatibles se omiten. El formato tiene límite de 64 MiB; cada registro conserva CRC32 y fuente exacta. Un archivo temporal seguido por `rename` atómico publica el índice.

## Concurrencia y memoria

`dyn86_init()` precarga el archivo antes de traducir, sin lock del dynarec ni GIL. Durante `FillBlock` sólo hay lookup bajo mutex breve y copias acotadas; las traducciones nuevas se copian a una lista inmutable en RAM. `atexit` serializa y sincroniza esa lista sin el mutex del dynarec. Dos procesos pueden competir por el último rename y perder hits, pero cada archivo queda íntegro o se descarta. SMC invalida por comparación exacta de bytes guest en cada lookup. La restauración alinea el bloque y sigue el protocolo ARM D-cache/I-cache del allocator existente.

## Criterios de aceptación

- [x] CA1: pruebas de la cache cubren cold write, warm hit, identidad/config chain, bytes fuente distintos, checksum corrupto/truncado y fallback; el juego real dio 268 hits por ejecución. La elegibilidad prueba B local, B externo, `BLX`, literal PC/VFP, ADR y UBFX ARMv7; el oracle aplica base host 0x41000000→0x42000000 y confirma miss seguro, sin mutación, para base no codificable.
- [x] CA2: `test_x87_oracle.sh` compara Wine x86 con QEMU ARM (13/13 iguales); cuatro pares del juego conservaron el SHA del primer frame cold/warm.
- [x] CA3: cuatro pares intercalados de menú QEMU, 2 s por corrida, identidad/build/settings fijos. 1.425 búsquedas, 268 hits, 1.157 miss/rechazos, cache 103.368 B; hash de frame `d056799d…9a62f`. Primer frame: mediana 221 ms frío/213,5 ms tibio; delta warm−cold por pareja +3,−9,0,−8 ms (mediana −4 ms), dentro del ruido observado. No se atribuye mejora de arranque.
- [x] CA4: el test verifica opt-in/configuración, corrupción, SMC por bytes fuente y mismatch de identidad/chain con fallback; sin habilitación no abre cache.
- [ ] CA5: `libwinx86` compila para qemu-arm y Vita; D2Vita conserva su build. La caché Vita queda desactivada hasta verificar que el formato de imagen y las direcciones host son estables allí.

## Decisión técnica

El corte medible de QEMU usa un ejecutable estático ET_EXEC, hash del binario exacto y configuración de chain coincidente. El validador falla cerrado ante literales PC y cualquier forma que no puede demostrar segura; las direcciones generadas de guest siguen siendo offsets guest, y el host base del arena se relocaliza por proceso. Esta ruta no demuestra portabilidad a Vita ni ahorro en Cortex-A9. Pass0 y parte de la lectura/copia siguen costando en caliente; la medición debe decidir si el ahorro restante vale el espacio/overhead.

## No objetivos

No cachear PIE/ET_DYN, código de Vita todavía no verificado, bloques SMC/`always_test`, CPU/x87, punteros de arena ni páginas guest. No aceptar un hit por nombre/tamaño/mtime. No atribuir reducción de inicio a entradas que no ejecute el dispatcher.
