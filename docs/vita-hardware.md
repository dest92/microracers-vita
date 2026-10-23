# PS Vita — especificaciones de plataforma (referencia para specs)
Nivel de evidencia: [público] = documentación pública (vitadevwiki/psdevwiki/VitaSDK); [consola] = medido por nosotros en d2vita.

## CPU
- SoC CXD5315GG: 4× ARM Cortex-A9 MPCore, ARMv7-A, Thumb-2, NEON + VFPv3 (D32). Usuario tiene 3 cores (0–2); el core 3 es del sistema. [público]
- Reloj: 333 MHz por defecto, hasta 444 MHz (scePowerSetArmClockFrequency). Homebrew suele fijar 444. [público]
- Caché: L1 32 KB I + 32 KB D por core, L2 compartida 2 MB (PL310... la doc pública dice 2 MB). Línea de 32 B. [público]
- Memoria secuencial sin PLD ≈ 43 ciclos/línea de 32 B (~330 MB/s por core); mispredict ≈ 11 ciclos. [consola, vita-pmu]
- PMU del A9 accesible desde usuario con `vpmu.skprx` (nuestro plugin). [consola]
- Sin JIT oficial: para el dynarec hace falta memoria ejecutable (kubridge / sceKernelAllocMemBlockForVM + taiHEN). D2Vita ya lo resuelve en winx86. [público+consola]

## Memoria
- 512 MB LPDDR2 principal; a la app le quedan ~ 365 MB (USER_RW) en apps normales, más con `ATTRIBUTE2` / `sceKernelAllocMemBlock` con tipos *_CDRAM / PHYCONT. [público]
- 128 MB CDRAM (VRAM, Wide I/O) de la GPU; acceso de CPU sin caché lento. [público]
- Bloques: USER_RW (cacheable), USER_CDRAM_RW, USER_MAIN_PHYCONT_NC_RW, USER_RW_UNCACHE. Tamaño múltiplo de 4 KB (CDRAM 256 KB). [público]
- Espacio x86 del guest: el PE del juego quiere base 0x400000 (sin ASLR) → winx86 traduce/relocaliza (tiene .reloc). [estático]

## GPU
- PowerVR SGX543MP4+ (4 cores), API nativa sceGxm (estilo Metal/libgcm). Shaders CG compilados a GXP (offline con psp2cgc o runtime con vitaShaRK / SceShaccCg). [público]
- Sin OpenGL nativo: vitaGL (Rinnegatamante) implementa GL 1.x/2.x/ES sobre GXM; D2Vita usa su renderer propio sobre sceGxm (reemplaza glide3x). [público+estático]
- Pantalla OLED/LCD 960×544, 60 Hz. [público]
- Texturas: hasta 4096×4096, formatos DXT1/3/5 (S3TC) soportados por hardware, PVRTC, ETC1. [público]

## Audio / input / IO
- Audio: sceAudioOut (puertos BGM/MAIN, 48 kHz, S16 estéreo/mono), latencia por grain (64–4096 muestras). sceAudiodec para MP3/AT9/AAC. [público]
- Input: sceCtrl (botones, 2 sticks analógicos), sceTouch (frontal + trasero), sceMotion. [público]
- Almacenamiento: ux0: (memory card / SD2Vita), IO lenta y con latencia; D2Vita vio tirones de hasta 438 ms en lecturas MPQ. [consola]
- Threads: sceKernelCreateThread, prioridad 64–191 (menor = más prioridad), afinidad por core. pthreads de VitaSDK (pte) hace polling de cancelación cada 500 µs → desactivar cancelación (WX86_NOCANCEL). [consola]

## Emulación (Vita3K) — validación sin consola
- Vita3K corre homebrew vpk; JIT de ARM en host (dynarmic). No emula exactamente la caché/tiempos → los números de rendimiento sólo valen en consola. [público]
