# HW-075 — contraste raw para futura calibración

Muestra nativa aislada, sin juego, PMU, audio ni GPU. Preparada y compilada; **no ejecutada físicamente y sin unidad de runClocks validada**. No modifica afinidad de music ni offloadea lógica guest.

## Artefacto final preparado
- `hotwheels/out/hw075-clock-calibration/v4/build/thread-clock.vpk` (TITLE_ID **HWCLOCK01**): 56.125 B, SHA256 `e8f9b3bd4ee2b04a7276b2fa248f04242042c3e4113c08948fa25647ca7f5064`.
- `v4/build/eboot.bin`: 59.503 B, SHA256 `fd12dfba44cf779beef6b7ae1b02ccfee5f18e72a2f567a304275e3deee18f01`.
- `v4/prebuild-manifest.json` y `v4/source/`: fuentes copiadas y hasheadas ANTES de compilar; se compiló desde esa copia. `v4/manifest.json` confirma hashes sin cambios después del build y registra binarios. `v4/build.log` contiene comando y resultado VitaSDK -Werror PASS. Versiones anteriores conservadas como supersedidas; usar v4.

**Alcance de consola:** la autorización vigente sólo cubre HWMR00002 y ux0:data/hotwheels. HWCLOCK01 es otra app: una futura instalación/ejecución requiere ampliar explícitamente ese alcance. No instalar, desplegar ni lanzar automáticamente. El orquestador debe cerrar primero el A/B OFFICE; esta preparación/revisión no requiere consola ni queda bloqueada por esa autorización futura. El VPK diagnóstico del juego b9b5cd4b…d8366a no fue reemplazado.

## Qué produce cuando se autorice una ejecución
Lanzar la app separada es el opt-in; no existe hook de producción ni flag del juego. Escribe `ux0:data/hotwheels/clock-calibration.log` (lo trunca al iniciar), hace los trials y sale sin pantalla gráfica.

Un único worker nativo por vez, cores solicitados USER_0/1/2 en secuencia. Por core: tres pares, orden busy/sleep, sleep/busy, busy/sleep. Son **18 filas**; 200.000 us nominales por fase, no duración CPU asumida. Busy itera sobre estado volatile hasta el deadline; sleep usa DelayThread. No solicita cambios de clocks ni prioridades. GetSystemTimeWide controla deadline, timestamps y timeout; runClocks queda crudo.

El observer captura sólo la consulta que observa worker WAITING, waitType no cero y waitId del semáforo de comando; nadie señala ese semáforo hasta terminar el snapshot. Así ambos extremos son puntos desplanificados. Esperas y sondeo acotados; error se registra y corta la muestra. El intervalo incluye wakeup/despacho/overhead, no se resta ni transforma automáticamente en CPU.

Cada fila separa:
- `integrity`: API/identidad/generación/tiempo/contador/barrera válidos. Sleep con delta cero está permitido.
- `core_target`: pin exitoso + máscara exclusivamente del core solicitado + último CPU coincidente en ambos extremos. Máscara cero/ambigua ⇒ unverified, conservando raw. No mide ocupación de core.
- `config_stable`: API válida, MHz positivos iguales y prioridad igual en ambos extremos. No prueba igualdad de condiciones entre distintas filas: comparar sus valores también entre cores/trials.

`unit=unverified` permanece en todos los casos. Una relación busy/sleep en host/Vita3K o en un único punto no valida frecuencia/unidad. La fase física CAL3 requiere repeticiones identificadas y clocks acordados por orquestador; no se publican CPU_us ni porcentajes antes de eso.

## Verificación local y reproducción de build
`hotwheels/tests/run_clock_calibration.sh`: host -O2/-Werror + ASan/UBSan; descubierto por run_all. Fixtures y mutantes cubren barrera, errores, core reservado incluso con máscara coincidente, ambas APIs, generación cero, MHz ausentes y extremos distintos. Reviews: `coordination/review-HW075-calibration-code.md` y addendum de `coordination/review-HW075-spec.md`.

Desde la raíz, para reconstruir sin modificar los artefactos conservados:

```sh
PATH="$PWD/shared/vitasdk/bin:$PATH" make \
  -C hotwheels/out/hw075-clock-calibration/v4/source/hotwheels/samples/thread-clock \
  BUILD="$PWD/hotwheels/out/hw075-clock-rebuild"
```

No se reutilizó una conversión previa: D2_SCHEDPROBE calibra iteraciones del scheduler; pmutest verifica PMU con plugin; frame_profile etiqueta runClocks como run_us sin una calibración de unidad encontrada. Ninguno reemplaza esta futura verificación física.
