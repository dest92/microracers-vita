# BUG-020 — QEMU SW salta a EIP nulo tras rechazar primaria al cambiar modo

Nivel: **medido en host (qemu)**, mimo, 2026-10-03. Severidad: mayor (corta toda corrida
~40 s; bloqueaba el soak HW-027). Estado: **cerrado** (`fbda786`, review APRUEBA).
HW-027 queda con owner QA.

## Evidencia
4/4 corridas `tools/soak.sh 2 sw` terminan igual:
`[hw] fin: box86 dynarec fault (unimplemented/illegal/div0) exit=0xc0000005 frames=223 missing=0`
con `[dynarec86] Run() interpreter stub hit at EIP=(nil) — aborting emulation`.
Runs: `out/run-20261003-043441`, `043649`, `043737`, `043838`.
Reportes: `out/soak-20261003-043{441,649,737,838}/report.md`.

Variables controladas (sin efecto, todas crashean a 223):
- sin env extra; `HW_SETTINGS_FILE=race-sw-settings.dat`; `HW_D3D=0` + settings.

Contraste: corridas anteriores `out/run-20261003-{013247,040257,041800,043301}` con el mismo
input (`race_enter.script`) llegan a `shutdown requested exit=0x00000000` con
219/219/298/264 frames. `043301` (OK) → `043441` (crash) es el rango donde aparece;
en ese tramo `tools/build_host.sh` tiene WIP local sin commitear.

## Impacto
- HW-027 (soak30 min) no puede cumplir criterio (1) hasta resolver esto.
- No afecta Vita3K (HW-029 usa GXP, carrera visible verificada).

## Repro
`bash hotwheels/tools/soak.sh 2 sw` en el árbol actual.

## Follow-up integ — 2026-10-04
Build SW en un worktree limpio de HotWheels `ff4881a` (su `build_host.sh` no incluye
`src/d3d/*` ni el cableado host D3D), con `race_enter.script` y el winx86 compartido
actual: reproduce el mismo fallo en frame 223, EIP=0 y los retornos guest
`00447e54`, `004112c6`, `004506ea`, `004507a0`. Por lo tanto el WIP de integración D3D
de HotWheels no es necesario para dispararlo. Este control aún usa el checkout winx86
`bd5d299` con modificaciones locales en dynarec/bridge; no atribuye causa raíz y se
necesita separar esas modificaciones antes de concluir sobre el dynarec.

## Follow-up integ — sitio probable del salto nulo — 2026-10-03

Análisis estático del repro guardado `run-baseline-ff4881a-20261004/stdout.txt`: EIP=0,
`ESP=0x0b1ffc40`, `EAX=0x0b1ffc68`, y `[esp]=0x00447e54`. En el PE, `0x00447e51`
es `call dword ptr [edx+0x1c]`, con retorno en `0x00447e54`; justo antes, EAX se carga
con `lea eax,[esp+0xc]` y se apilan seis palabras incluyendo el retorno, lo que explica
la diferencia `EAX-ESP=0x28`. Ese sitio es `IDirectDrawSurface4::BltFast` (slot 7,
cinco argumentos explícitos). La tabla local declara `abi::surface4[7]=5` y `surf_call`
implementa slot 7, por lo que el conteo estático coincide con la firma.

Esto acota el fault a la llamada indirecta de BltFast y sugiere que el target leído de
`[edx+0x1c]` era cero, pero el log no guarda EDX, EDI ni el contenido del vtable y no
prueba si hubo superficie/vtable corrupta o fallo del dynarec. Siguiente evidencia útil:
capturar esos tres valores antes del `call` en una traza temporal del bridge/CPU. No
modifiqué el WIP compartido de winx86 ni la ruta DDraw.

## Follow-up integ — captura dinámica aislada — 2026-10-03

Repetí dos veces el modo SW con el checkout winx86 actual copiado a
`out/winx86-diag` y una impresión temporal sólo en esa copia. Ambos runs terminan en
frame 223, `EIP=0`, `missing=0`; logs `out/run-20261003-055058/stdout.txt` y
`out/run-20261003-055250/stdout.txt`. El segundo dump da `ESI=0x00473c90`,
`[ESI+0x45c]=0`, `[ESI+0x460]=0`, `EDI=0`, y el slot leído en `[EDX+0x1c]=0`.

El PE confirma en `0x00447e2e` que EDI se carga desde `[ESI+0x45c]`, en `0x00447e42`
que se lee el vtable desde `[EDI]`, y en `0x00447e51` que se llama su slot `+0x1c`
(BltFast). Por tanto el fault observado no demuestra un defecto del dynarec ni una
aridad incorrecta: el propio guest alcanza BltFast con su campo de surface nulo y
salta a target cero. Los dos campos se ponen a cero en la inicialización del objeto
(`0x00446cf1..0x00446cfd`); falta localizar por qué el flujo de carrera los usa sin
haberlos poblado. BUG-020 sigue abierto. Próximo paso para runtime/integ: seguir el
flujo que debía poblar `[ESI+0x45c]` antes de `0x00447e10`; no parchear el dynarec por
este síntoma.

## Cierre — 2026-10-03

La traza DDraw temporal confirmó que `SetDisplayMode(800,600,8)` no soltaba la
designación de primaria de 640×480. El shim rechazaba el segundo `CreateSurface`
(`DDERR_UNSUPPORTED`), el guest seguía con su campo de surface nulo y entraba a
`BltFast` vía target cero. `fbda786` resetea la primaria activa en cada cambio de modo
válido; la revisión estática de `fbda786` + `6730556` APRUEBA.

Fix complementario `47fe6d4` quita también `DDSCAPS_PRIMARY` del objeto viejo, para que
`GetCaps` lo reporte no-primary y `Flip`/render target lo rechacen mientras siguen vivas
las referencias COM. Review del segundo cambio y prueba `6a708fb`: APRUEBA.

Verificación: `test_ddraw_bridge` PASS, incluyendo caps/Flip/target antiguos y primaria
nueva con backbuffer. QEMU SW `out/run-20261003-060001` antes del ajuste de caps finalizó
con shutdown limpio, 469 frames y `missing=0`; con el ajuste final, `060841` llegó a
451 frames, mostró carrera en `frame_00450.png` y no reprodujo el fault, pero no registró
la línea final de shutdown. BUG-020 cerrado; HW-027 queda para el soak de QA.
