# BUG-008 — boot integrado Vita3K: acceso NULL antes de present

Estado **cerrado** (review APRUEBA), HW-007, owner integ. Evidencia **emulador**, no consola.

Dos intentos: `build-vita3k/hotwheels-vita-20261003-001932` y
`hotwheels-vita-20261003-002429`, harness 35/25 s respectivamente.
VPK instalado y ejecutado; segundo run con sceClibPrintf confirma 94 imports,
0 sin shim, scheduler nativo y lectura correcta de DATA/LANG.TXT. Última llamada
trazada: CreateFileA(data/menu/settings.dat) devuelve -1 (archivo ausente,
también tolerado en host). Vita3K cae con SIGSEGV acceso 0x0 antes del present;
shot_020.png del primer run inspeccionada: negra.

No confundir con BUG-005 (sample present alcanza frame0 y cae en otro acceso).
Siguiente diagnóstico: DSOUND #1 sigue en orden de boot; pedir instrumentación a
fixer y comparar consola autorizada. No modificar plugins ni apps ajenas.

Diagnóstico run `002952` con backend pool corregido: audio.log termina en
`engine: start ...` (no `start ok`); ds_emul sí se registró y abrió el sink Vita.
Fixer investiga esa ruta; `HW_AUDIO=0` permite aislar boot gráfico manteniendo
el stub DSERR_NODRIVER conocido (audio ON por default).

## Fixer (105b687): parte de audio resuelta
El cuelgue en `engine: start ...` era SIGSEGV DENTRO del `sceAudioOutOpenPort` de Vita3K headless (sin dispositivo
de audio). `tools/vita3k_run.sh` ahora usa `SDL_AUDIODRIVER=dummy`; fixture `samples/audio-engine` completa el ciclo.
Re-correr el boot integrado con HW_AUDIO=1: si cae, es otro acceso NULL (no audio).

Validación posterior: harness `004225`, HW_AUDIO=1 + SDL_AUDIODRIVER=dummy
(fixer), sink Vita con pthread de fixer: engine:start ok, primer PCM escrito,
>1300frames640×480 en25s y menú real visible en shot015 (inspeccionado).
No SIGSEGV observado. Música no presente en stage, no se afirma audio audible.
