# SPEC-005 — WINMM: MMIO RIFF/WAV + MCI `cdaudio` (HW-015)

- Feature: HW-015 · Milestone: M2 · Dueño: `deepseek` · Estado: borrador
- Depende de: HW-005 (esqueleto/bridge). No depende de DDRAW ni del renderer.
- Fuentes: `docs/gap-list.md` §WINMM, `docs/re-engine.md` §4 (Audio), `docs/wine-reference.md` §5/§6,
  `docs/triage.md` §55, `docs/winx86-inventory.md` §Cómo agregar una shim.

## Problema

`HotWheels.exe` importa 11 símbolos de `WINMM.dll` (`docs/triage.md` §88):

- `mmioOpenA`, `mmioRead`, `mmioSeek`, `mmioDescend`, `mmioAscend`, `mmioClose`,
  `mmioGetInfo`, `mmioSetInfo`, `mmioAdvance`.
  `dsLoadWav` (0x44f0f0) y `wavOpenMmio` (0x44ffb0) parsean `RIFF/WAVE` con estas funciones antes de
  crear cada buffer secundario de DirectSound. Sin ellas, el juego no puede cargar **ningún** WAV.
- `timeGetTime`: ya cubierto por la familia reloj de HW-006; esta spec **no** lo implementa.
- `mciSendCommandA`: `cdOpen` (0x44e690) abre el dispositivo `cdaudio` (letra de CD), fija formato
  `MSF/TMSF` (`MCI_SET`), lee la tabla de contenidos (`MCI_STATUS`) y reproduce pistas (`MCI_PLAY
  FROM/TO`, `MCI_PAUSE`, `MCI_STOP`, `MCI_CLOSE`). La música del juego es **CD audio**, no un WAV de
  `DATA\SOUND` (`docs/re-engine.md` §4, `docs/wine-reference.md` §5).

En Vita no hay unidad de CD. El port debe emular `cdaudio` reproduciendo las pistas extraídas del
repack (`MUSIC/Track N.wav`) o silencio si el archivo falta, sin bloquear el menú.

## Contrato observable

La unidad es **portable C++17** en `hotwheels/src/winmm/`, sin dependencias de Windows ni de winx86.
Todo el I/O físico se hace por interfaces abstractas inyectadas; el backend de audio real (Vita:
`sceAudio`, host: null/mixer) queda **fuera** de esta spec.

### Interfaces inyectadas

```cpp
namespace hw::winmm {
// Acceso a archivos Win32 (rutas con '\' o '/', case-insensitive en el port).
struct FileSource {
    virtual ~FileSource() = default;
    // Devuelve true y llena `out` si el archivo existe; false si no.
    virtual bool read(const std::string& win_path, std::vector<uint8_t>& out) = 0;
};
// Salida de música. La implementación decide decodificar/mostrar o reproducir silencio.
struct MusicSink {
    virtual ~MusicSink() = default;
    virtual void play(int track, const std::string& path, uint32_t from_tmsf, uint32_t to_tmsf) = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
};
}
```

### MMIO (`mmio.h`, handle opaco `HMMIO = uint32_t`, 0 = inválido)

Subconjunto de `mmiscapi.h` (MinGW `shared/opt/llvm-mingw-*/include/mmiscapi.h`), con las mismas
constantes y el mismo layout de `MMIOINFO`/`MMCKINFO`:

- `HMMIO open(const char* filename, MMIOINFO* info, uint32_t flags)`
  - `flags & MMIO_READ` (0) lee de `FileSource`; otras combinaciones no se soportan.
  - Si el archivo no existe: devuelve `0` y, si `info != nullptr`, `info->wErrorRet = MMIOERR_FILENOTFOUND`.
  - Si tiene éxito: `wErrorRet = 0`, `lDiskOffset = 0`.
- `int32_t read(HMMIO, void* buf, int32_t count)` → bytes leídos; en EOF `0`; handle inválido `-1`.
  No cruza el límite del chunk RIFF descendido (Win32 real no lo hace tampoco: `mmioRead` lee del
  buffer del IOProc, acotado por el chunk). El test lo exige.
- `int32_t seek(HMMIO, int32_t offset, int origin)` con `SEEK_SET=0/SEEK_CUR=1/SEEK_END=2`;
  devuelve el nuevo offset absoluto o `-1`.
- `uint32_t descend(HMMIO, MMCKINFO& ck, const MMCKINFO* parent, uint32_t flags)`
  - `MMIO_FINDRIFF`/`MMIO_FINDLIST`: lee `ckid`, `cksize`; el `fccType` son los 4 bytes siguientes;
    `dwDataOffset` = offset del payload. `MMIO_FINDRIFF` exige `fccType == 'WAVE'` para `RIFF`.
  - `MMIO_FINDCHUNK`: busca `ck.ckid` dentro del rango de `parent` (si es null, desde la posición
    actual hasta el final del archivo). Devuelve `MMIOERR_CHUNKNOTFOUND` (265) si no aparece.
  - Al descender, la lectura (`read`) queda acotada al `cksize` del chunk.
- `uint32_t ascend(HMMIO, MMCKINFO& ck, uint32_t flags)`: posiciona `lDiskOffset` en
  `ck.dwDataOffset + ck.cksize` (alineado a par) y desapila el chunk. `0` en éxito.
- `uint32_t get_info(HMMIO, MMIOINFO&)` / `uint32_t set_info(HMMIO, const MMIOINFO&)`:
  exponen/restauran `lDiskOffset`, `dwFlags`, buffer (`pchBuffer/pchNext/pchEndRead/lBufOffset/cchBuffer`).
  `set_info` con `pchNext` dentro del buffer mueve la posición lógica al byte apuntado.
- `uint32_t advance(HMMIO, MMIOINFO&, uint32_t flags)`: si hay buffer (`cchBuffer > 0`), lo recarga
  desde `lBufOffset` y actualiza `pchNext`/`pchEndRead`; en EOF deja `pchNext == pchEndRead`. Sin
  buffer, no-op que devuelve `0`.
- `uint32_t close(HMMIO)`: libera el handle (a partir de ahí es inválido). `0` en éxito.

Los helpers `FOURCC('R','I','F','F')`, etc. y las constantes se declaran en `winmm.h`.

### MCI `cdaudio` (`mci.h`)

`mciSendCommandA` se modela como `MCIERROR send_command(MCIDEVICEID id, uint32_t msg, uint32_t flags,
void* parms)`. Constantes y structs idénticos a MinGW `mciapi.h` (verificados en el header local):

- `MCI_OPEN=0x0803`: `parms = MCI_OPEN_PARMS*`. Con `flags & MCI_OPEN_TYPE` y
  `lpstrDeviceType == "cdaudio"` (case-insensitive) crea un dispositivo y escribe su id (>0) en
  `wDeviceID`; devuelve `0`. Otro tipo: `MCIERR_DEVICE_TYPE_REQUIRED` (287) si falta `deviceType`,
  o `MCIERR_UNSUPPORTED_FUNCTION` (274) si no es `cdaudio`.
- `MCI_SET=0x080D`: `flags & MCI_SET_TIME_FORMAT` guarda `parms->dwTimeFormat` (acepta
  `MCI_FORMAT_MSF=2` y `MCI_FORMAT_TMSF=10`); devuelve `0`.
- `MCI_STATUS=0x0814`: requiere `flags & MCI_STATUS_ITEM`; `parms->dwItem` puede ser
  `MCI_STATUS_NUMBER_OF_TRACKS`, `MCI_STATUS_CURRENT_TRACK`, `MCI_STATUS_LENGTH`,
  `MCI_STATUS_POSITION` o `MCI_STATUS_MODE`. Escribe `dwReturn` (y `dwTrack` donde aplique) y
  devuelve `0`.
- `MCI_PLAY=0x0806`: `parms = MCI_PLAY_PARMS*`. Si `flags & MCI_FROM`, la pista es
  `MCI_TMSF_TRACK(dwFrom)` (**byte bajo**); si no, la pista actual (o 1). Marca `playing`, resuelve
  `path = track_path(track)` y llama `sink.play(track, path, dwFrom, flags&MCI_TO ? dwTo : 0)`.
  Devuelve `0` aunque el WAV no exista (el `MusicSink` reproduce silencio).
- `MCI_PAUSE=0x0809`: `sink.pause()`, `0`. `MCI_STOP=0x0808`: `sink.stop()`, `0`.
- `MCI_CLOSE=0x0804`: `sink.stop()`, libera el dispositivo, `0`.
- `id` desconocido/cerrado → `MCIERR_INVALID_DEVICE_ID` (257). `msg` desconocido →
  `MCIERR_UNRECOGNIZED_COMMAND` (261).

### TOC y mapa pista → archivo

El ISO del repack es **sólo datos**; los 4 WAV de audio vienen aparte (`docs/triage.md` §55). El
mapeo observado en `extracted/music/` es 1:1 y se adopta como contrato:

| pista | archivo (ruta Win32) |
|---:|---|
| 1 | `MUSIC\Track1.wav` |
| 2 | `MUSIC\Track2.wav` |
| 3 | `MUSIC\Track3.wav` |
| 4 | `MUSIC\Track4.wav` |

`track_path(n)` usa la ruta canónica con `\`; el `FileSource` del port la resuelve
case-insensitive y traduce `\`→`/`. La correspondencia pista CD ↔ archivo del repack no está
confirmada más allá del nombre (`triage.md` §55); por eso la TOC es una tabla configurable con
**default = duraciones medidas** de los WAV (`T1 312.37 s`, `T2 313.37 s`, `T3 264.64 s`,
`T4 282.61 s`) y el contrato sólo exige que `MCI_STATUS` informe `4` pistas y que `MCI_PLAY FROM`
de la pista *n* invoque al sink con la pista *n*. El default documenta la fuente, no prueba la
autoría del CD original.

## Criterios de aceptación (cada uno ⇒ un test host, `hotwheels/tests/test_winmm.cpp`)

1. **CA1** — `mmioOpenA` de un WAV sintético (generado en el test, PCM) devuelve handle válido y
   `lDiskOffset == 0`; abrir un archivo inexistente devuelve `0` con `MMIOERR_FILENOTFOUND`.
2. **CA2** — `mmioDescend(MMIO_FINDRIFF)` expone `ckid='RIFF'`, `fccType='WAVE'`, y `cksize`
   correcto; `descend` de un chunk inexistente devuelve `MMIOERR_CHUNKNOTFOUND`.
3. **CA3** — `mmioDescend(MMIO_FINDCHUNK)` de `fmt ` y `data` con `parent = RIFF` posiciona
   `dwDataOffset` y `cksize` correctos; `read` de 16 bytes de `fmt ` decodifica PCM/mono/22050/16.
4. **CA4** — `read` no cruza el borde del chunk `data`: pedir más bytes que el `cksize` devuelve sólo
   los disponibles (y `seek`/`read`/`ascend` continúan coherentes después).
5. **CA5** — `seek` (SET/CUR/END) devuelve offsets correctos; `get_info` refleja `lDiskOffset`;
   `set_info` restaura una posición previa; `advance` sin buffer es no-op `0`.
6. **CA6** — `mmioClose` invalida el handle: una operación posterior devuelve error (`0`/`-1`).
7. **CA7** — `mciSendCommandA(MCI_OPEN)` con tipo `"cdaudio"` devuelve `0`, asigna `wDeviceID > 0`;
   tipo distinto o id inválido devuelven el MCIERROR documentado.
8. **CA8** — `MCI_SET` con `MCI_FORMAT_TMSF` y `MCI_STATUS(NUMBER_OF_TRACKS)` informan `tmsf` y `4`.
9. **CA9** — `MCI_PLAY` con `FROM = MCI_MAKE_TMSF(2,0,0,0)` llama `sink.play(2, "MUSIC\\Track2.wav", …)`;
   pista por defecto sin `MCI_FROM` es 1; `MCI_STATUS(CURRENT_TRACK)` refleja la última pista.
10. **CA10** — `MCI_PAUSE`/`MCI_STOP`/`MCI_CLOSE` llaman `sink.pause/stop` en ese orden y `CLOSE`
    deja el id inválido (otra llamada → `MCIERR_INVALID_DEVICE_ID`).
11. **CA11** — Reproducir una pista **no** invoca `FileSource`: si el WAV falta, `send_command`
    devuelve `0` igual (contrato de "silencio, no bloquea").
12. **CA12** — Las constantes `MMIO_*`, `MCI_*`, `MCIERR_*` y `MMIOERR_*` valen lo documentado en
    `mmiscapi.h`/`mciapi.h` (static_assert) y los helpers `MCI_MAKE_TMSF`/`MCI_TMSF_*` hacen
    round-trip track/min/sec/frame (el struct portable es de campos 32-bit porque el guest es PE32).

## Diseño (bajo nivel)

- Sin threads ni estado global: `Mmio` y `Cdaudio` son objetos con estado propio; los handles son
  índices crecientes (`HMMIO`, `MCIDEVICEID`) para que el futuro shim los traduzca a/desde el guest.
- `Mmio::File` guarda el archivo completo en `std::vector<uint8_t>` (el repack carga WAV estáticos;
  no hay streaming), `pos`, y una pila de chunks (`{payload_off, size}`) para acotar `read`/`ascend`.
- `Mmio` no conoce el disco: `FileSource::read` resuelve la ruta case-insensitive y el prefijo
  `MUSIC\`/`DATA\` lo aplica el `FileSource` del port (fuera de esta unidad).
- `Cdaudio` guarda `std::map<MCIDEVICEID, Device>`; `Device` tiene `time_format`, `current_track`,
  `playing`, `paused`, TOC (tabla de duraciones) y `MusicSink*`. No decodifica audio.
- ABI eventual (documentado, no implementado acá): `Bridge::register_shim("WINMM.dll", …)` con
  `argc`/`stdcall_cleanup=true`; `mciSendCommandA` = 4 args `stdcall` (limpia el callee). Los shims
  leen punteros guest y marshallan a estas clases. Integración = feature posterior (HW-006).

## No-objetivos

- `timeGetTime`/`timeBeginPeriod` (reloj de HW-006), DirectSound, mezcla/decodificación real de audio,
  backend Vita (`sceAudio`), streaming de WAV, escritura (`MMIO_WRITE`/`MMIO_CREATE`),
  `MCI_SEEK`/`MCI_INFO`/`MCI_GETDEVCAPS` y los mensajes no listados.
- No se toca winx86 ni `build_vita.sh`: la unidad se compila host-side por ahora.

## Riesgos / preguntas abiertas

- La correspondencia pista CD → archivo es por nombre; confirmarla con HW-012 (traza Wine de una
  carrera) o con la TOC real del CD. Si el juego mapea `menu=Track2`, etc., ajustar la tabla, no la API.
- `MCI_STATUS(LENGTH/POSITION)` en MSF/TMSF: las duraciones default son de los WAV del repack; el
  contrato no fija valores, sólo formato y nº de pistas.
- ¿`mmioGetInfo/SetInfo/Advance` en la ruta real? `gap-list.md` lo deja a verificar con traza; se
  implementan y testean igual porque son parte del import set.

## Evidencia

- `[estático]` nombres y aridades: `docs/triage.md` §88 y anexo de `docs/re-engine.md`.
- `[estático]` constantes/structs: headers MinGW locales (`mciapi.h`, `mmiscapi.h`).
- Resto: test host (esta spec). Wine/Vita3K quedan para HW-006/HW-007.
