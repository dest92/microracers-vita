# HW-071 — RE de audio (DirectSound) (REA + Ghidra 12.1.4)
- Autor: `rea-re` · Fecha: 2026-10-21 · Evidencia: **[estático]** si no se indica. Binario SHA-256 `5418a6aa…1122`, base `0x400000`.
- Complementa `docs/re-engine.md` §4 (que ya fijó DSound exclusivo, primario 22050/16/estéreo, buffers estáticos y música por MCI).

## 1. Modelo de mezcla: el motor NO mezcla; mezcla DirectSound
**Conclusión central**: el exe **nunca escribe PCM en el buffer primario** más allá del `SetFormat`, y **no hay
callbacks de streaming, ni threads de mezcla, ni `IDirectSoundNotify`** (`DSBCAPS` no pide `CTRLPOSITIONNOTIFY`).
Toda la mezcla la hace DirectSound: el motor solo `Play()`ea **buffers secundarios estáticos** (uno por WAV) y el
driver suma. Por lo tanto **el shim de la Vita (ds_emul) tiene que implementar el mezclador**: sumar hasta N voces
simultáneas, cada una con su frecuencia/volumen/pan.
> **Nivel de evidencia (precisión pedida en review `review-fd8754e-audio.md` #1):** "el motor no escribe PCM en el primario"
> es una negación apoyada en los flags del `DSBUFFERDESC` y en la ausencia de imports de notificación/streaming, **no** en una
> traza. Corroborarla con la captura de SPEC-048 CA1 (que registra `Lock`/`Unlock` del primario): si aparece un `Lock` de
> escritura sobre el primario, este claim cae.

- `dsInit` (`FUN_0044efd0` `0x44efd0`): `DirectSoundCreate` (ordinal #1) → `SetCooperativeLevel(3=DSSCL_EXCLUSIVE)`
  → `CreateSoundBuffer` del **primario** → `SetFormat(WAVEFORMATEX: wFormatTag=1 PCM, nChannels=2, nSamplesPerSec=22050
  (0x5622), nAvgBytesPerSec=88200 (0x15888), nBlockAlign=4, wBitsPerSample=16, cbSize=0)`; marca `this[0xe] (byte +0x38)=1`
  ("habilitado", chequeado en todas las operaciones) y `this[0x73] (int +0x1cc)=0` (**contador de voces activas**).
- Estructura del gestor (`this`): `+0x00` = `IDirectSound*`, `+0x04` = buffer primario, `+0x3c` = **array de hasta 100
  punteros a voz** (`this + i*4 + 0x3c`), `+0x1cc` = cantidad de voces.
  → **límite duro de 100 voces concurrentes** (`dsPlayVoice` rechaza si `count >= 100`).

## 2. Carga de WAV → buffer estático (`dsLoadWav` `FUN_0044f0f0` `0x44f0f0`)
1. `wavOpenMmio` (`0x44ffb0`) con `mmioOpen` + `mmioDescend` a `RIFF/WAVE`; `wavReadMmio` (`0x44fc60`) lee el bloque
   `data` completo (tamaño `*(this+0x14)`) a un `operator_new`.
2. `DSBUFFERDESC`: `dwSize=0x24`, **`dwFlags=0xE2`** = `DSBCAPS_STATIC(0x2) | CTRLFREQUENCY(0x40) | CTRLVOLUME(0x80) | CTRLPAN(0x20)`.
   **Sin `CTRL3D`, sin `CTRLPOSITIONNOTIFY`, sin `LOCDEFER`** (mezcla siempre en software del driver).
3. `IDirectSound::CreateSoundBuffer` (vtable +0xc) → `Lock` (+0x2c) `(0, &ptr, &bytes, …)` → `memcpy` del PCM →
   `Unlock` (+0x4c) `(ptr, bytes, 0)`.
4. Devuelve una **voz** de 0x18 B: `+0x00` = `IDirectSoundBuffer*`, `+0x04` = flag "tiene copia", `+0x05` = "sonando",
   `+0x06` = "loop", `+0x07` = "no duplicar si ya suena", `+0x08` = slot de volumen, `+0x0c` = slot de pan,
   `+0x10` = valor de frecuencia, `+0x14` = el buffer (usado por `dsGetBuffer`).
   Si no hay DSound (`DAT_00473730==0`) la carga devuelve `NULL`.

## 3. Reproducción de una voz (`dsPlayVoice` `FUN_0044fa00` `0x44fa00`)
| Llamada (vtable `IDirectSoundBuffer`) | Argumentos | Efecto |
|---|---|---|
| `DuplicateSoundBuffer` (`FUN_0044f260`) | — | clona el buffer si la voz ya está sonando y no se pidió "no duplicar" |
| `SetCurrentPosition` (+0x34) | `0` | rebobina |
| `SetFrequency` (+0x44) | `param_2` | **la frecuencia se fija por voz, en cada `play`** |
| `SetVolume` (+0x3c) | valor clamp `[-10000, 0]` (centésimas de dB) | volumen = `arg - 300`, saturado a `-10000` |
| `SetPan` (+0x40) | `0` en este camino | pan centrado |

- Las voces se registran en el array (`+0x3c`, tope 100) y el motor las reutiliza/recicla; `SetVolume`/`SetPan` reciben
  valores en **centésimas de dB** (`DSBVOLUME_MIN = -10000`).
- `dsGetBuffer` (`FUN_0044fbf0` `0x44fbf0`) devuelve `*(this+0x14)` del buffer o `0` si DSound no está habilitado.
- **Para BUG-031 (precisión pedida en review `review-fd8754e-audio.md` #2)**: lo **evidenciado** es que `dsPlayVoice` fija
  `SetFrequency` **en cada `play`**; **no** está evidenciado que el motor cambie la frecuencia *dentro* de una misma reproducción
  (eso requeriría una llamada al `+0x44` fuera del camino de `play`, que no encontré). Por lo tanto: el requisito firme es un
  **rate por voz** (lo que cubre SPEC-048 CA2), y el "cambio por muestra" sólo hace falta **si** el motor re-`play`ea a alta tasa
  (entonces sí se querría interpolación con fase continua entre re-disparos, que es lo que implementó opus). El RE confirma que
  `SetFrequency` es la palanca del pitch; **no** confirma por sí solo la necesidad de resampleo por muestra.

## 4. Ownership, orden de memoria y bordes (razonamiento SO/hardware)
Derivado del mecanismo del exe (§1-§3); es el contrato de `ds_emul`/`Mixer`, no código nuevo.

### 4.1 Quién es dueño del PCM
`dsLoadWav` copia el WAV entero al buffer de DSound (`Lock`→`memcpy`→`Unlock`) y **libera el buffer temporal del guest**
(`FUN_00452740`). Después de eso el guest **no vuelve a tocar** ese PCM: no hay escrituras por frame ni re-Lock. → El shim puede
mantener el PCM como **solo-lectura inmutable** mientras la voz viva; no hace falta doble-buffer por voz ni copia por mezcla.
La voz (parámetros) sí es mutable: ver 4.2.

### 4.2 Concurrencia: el hilo del guest vs. el hilo del mezclador
El guest es **monohilo** (re-engine §0), pero el mezclador vive en un hilo del shim (sink `sceAudioOut`). Por lo tanto
`Play`/`Stop`/`SetCurrentPosition`/`SetFrequency`/`SetVolume`/`SetPan` (que el guest llama desde su hilo de render) **comparten
estado con el mezclador**:
- La **voz** (posición de lectura, rate, ganancia, pan) es el recurso compartido. Publicar los parámetros cambio-por-cambio con
  release/acquire, o tomarlos como **snapshot al comienzo de cada bloque de mezcla**, evita que el mezclador lea el rate nuevo con
  la posición vieja (o un `frequency` a medio escribir) y produzca un salto de fase audible.
- El **anillo de salida** (PCM mezclado) es productor (hilo mezclador) / consumidor (sink): el índice de escritura se publica con
  **release** y el de lectura con **acquire** — en ARMv7 (Cortex-A9) el modelo es débil y publicar el índice sin barrera deja al
  sink leer un slot cuyos bytes todavía no están visibles. Reusar el `DsoundBus`/`Mixer` ya existentes (spec 036/048); **no**
  inventar un segundo mecanismo de pacing.

### 4.3 Bordes (0, máximo, clase de error)
- **Tope de voces 100**: `dsPlayVoice` **rechaza** la voz (no la encola) si `count >= 100`; el shim debe descartarla igual, no
  crecer sin límite ni pisar una voz viva.
- **DSound deshabilitado**: si `*(char*)(this+0x38) == 0`, `dsPlayVoice` es **no-op** y `dsGetBuffer` devuelve `0`. El shim debe
  reproducir ese contrato (audio apagado ⇒ silencio, sin crash y sin retener handles); los llamadores del exe ya manejan el `0`.
- **Volumen**: `arg - 300` y luego clamp a `[-10000, 0]` (centésimas de dB). El clamp es **saturante**; el shim no debe mapear
  linealmente ni dejar pasar valores < -10000 (amplificación).
- **Frecuencia**: `param_2` es el valor crudo de `SetFrequency` (Hz del buffer). El paso de resampleo debe derivarse con guarda
  `hz > 0`; un 0 daría paso 0 (silencio/división) y **no** es un caso imposible si el guest pasa un valor sin filtrar.
- **`SetPan(0)`** en este camino: no implementar un paneo que el motor no pide (pero dejar el punto de entrada si el guest lo usa,
  §6 pendiente 2).

## 5. Música
CD-audio por **MCI** (no DSound): `cdOpen` (`0x44e690`) `MCI_OPEN` tipo `"cdaudio"` con la letra del CD
(`DAT_0047510c`), `MCI_SET` a formato de tiempo MSF, `cdReadTOC` (`0x44ea00`) vía `MCI_STATUS`, `cdPlayTrack`
(`0x44e750`) `MCI_PLAY FROM|TO`, `cdPause` (`0x44e8d0`) `MCI_PAUSE` (llamado en `WM_ACTIVATE`), `MCI_CLOSE`.
→ **El motor no tiene música propia en DSound**; los WAV de `data\sound\{game0,game1,vehicles0,vehicles1}` y
`data\menu\sound` son efectos. El repack agrega 4 WAV musicales grandes (206.918.056 B) que el `_inmm` del repack
enruta al MCI. El shim de música debe ser **MCI**, no DSound.

## 6. Pendientes
1. **Prioridad (cierra la única cifra inferida; precisión #3 del review)**: contar y listar `dsLoadWav`/`dsPlayVoice` por escena
   (pista/menú) para dimensionar el mezclador — hoy el tope es 100 voces y Wine midió ~6 secundarios en menú; falta el número de
   carrera. Es una medición de Wine/qemu (oráculo HW-069), no de RE estática.
2. Verificar si `SetVolume`/`SetPan` se usan con valores != 0 desde otros callers de `dsPlayVoice` (acá el pan queda en 0).
3. Confirmar el rol de `data\sound\silence.wav` en loop (re-engine §1 lo menciona en `initHotWheels`; no aparece en `dsInit`).
4. ~~Buscar `SetFrequency` (+0x44) fuera de `dsPlayVoice`~~ → **RESUELTO**: en todo el `.text` hay sólo **3** sitios `call [reg+0x44]`
   (`0x444b49`, `0x44faa2`, `0x44fb48`) y **dos de ellos están dentro de `dsPlayVoice`** (`0x44faa2` y `0x44fb48`); el restante
   (`0x444b49`) usa otro objeto (zona DDraw/D3D, no DSound). → La frecuencia se fija **sólo** en el camino de `play`; no hay
   cambio intra-reproducción → refuerza la precisión #2 del review (basta **rate por voz**).
