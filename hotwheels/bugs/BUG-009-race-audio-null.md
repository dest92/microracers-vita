# BUG-009 — navegación hacia carrera: SoundBuffer Stop NULL
- Severidad: bloqueante · Estado: cerrado · Feature: HW-026 · Dueño: fixer · Nivel: emulador qemu-arm

Dos intentos reproducibles: out/run-20261003-003742 y -003952 (TRAPTAG=1),
`HW_INPUT_SCRIPT=/work/hotwheels/tools/race_enter.script`, límite30s.
Frames=221, missing=0; frame220 muestra selector de pista (arena de arena),
no una carrera. Tras Return en frame220, guest EIP=0, EAX=0, ESP=0x0b1ffc5c,
retaddr0x44f3b2 y caller0x42458d; scheduler reporta fault0xc0000005.

Disasm estático en 0x44f3af: llamada indirecta vtable+0x48 (slot18,
IDirectSoundBuffer::Stop); objeto buffer leído de sample+0x14.
Contexto audio0x4736f8, flag+0x38=0x473730. Posible buffer secundario NULL;
no concluir opcode faltante del dynarec a partir del mensaje genérico.
TRAPTAG registra buffer primario CreateSoundBuffer/SetFormat/Release y MMIO,
pero no creación de secundarios antes de esa transición (traza capada a4000).

Fixer recibió evidencia para investigar marshalling/lifecycle de audio.
Guion input portable verde; mientras tanto integrar seam D3D6 independiente.

## Resolución (fixer, 28f07aa)
Causa: `Mmio::open` ignoraba `MMIO_ALLOCBUF`. El loader (patrón wavread.c del DX SDK) hace GetInfo → copia bytes
avanzando pchNext → Advance al agotar; con cchBuffer=0 Advance no cargaba nada, el WAV se daba por corrupto y
NUNCA se creaba el IDirectSoundBuffer secundario (trace: 1 sólo CreateSoundBuffer, el primario). Además get_info
dejaba lBufOffset=0 tras descend → el set_info previo a Advance volvía al inicio del archivo.
Fix (src/winmm/mmio.cpp): ALLOCBUF reserva MMIO_DEFAULTBUFFER (8 KiB) o el cchBuffer pedido; get_info re-ancla
un buffer consumido en la posición actual. Test: tests/run_winmm.sh (caso BUG-009).
Resultado: 7 CreateSoundBuffer (1 primario + 6 efectos), 6 Lock/Unlock, missing=0. La regresión `bash tests/run_winmm.sh` pasa (CA1..CA12). Integ confirmó después la transición a carrera en `run-20261003-004748`.

## Verificación (fixer): guion `race_enter.script`, 40 s, nativo → **CARRERA** (frame_00634: cronómetro 00:20:64, HUD), 635 frames, missing=0, sin fault. hotwheels/out/run-20261003-004748

Fixer corrigió MMIO_ALLOCBUF en28f07aa. Corrida `004443` supera frame221,
termina por timeout controlado30s con461frames/missing0; frame460 muestra carrera
software640×480 (inspección visual). Bridge D3D registrado, sólo sonda HAL se
ejercitó: settings default software. Carrera D3D aún pendiente con overlay.


## Cierre vigente (2026-10-03)
`28f07aa` implementa `MMIO_ALLOCBUF` y reancla `get_info`; el test de WINMM confirma lectura WAV y el QEMU de 40 s llegó a carrera (frame 634, 635 frames, missing=0, sin fault). Review `coordination/review-28f07aa.md` APRUEBA. La línea histórica de crash del selector describe el síntoma previo al fix.
