# HW-051 — perfil fiel por PC host

## Método y evidencia

`HW_PC_PROFILE=1` instala un timer Linux `CLOCK_THREAD_CPUTIME_ID` periódico de 1 ms con `SIGEV_THREAD_ID`, dirigido al TID que ejecuta `ns->run()`. El handler toma el PC ARM del `ucontext` de ese hilo. Al cerrar, `FindDynablockFromNativeAddress(PC)` resuelve PC host→dynablock y `instsize[]` interpola a EIP guest; el CSV Ghidra (877 rangos, 825 funciones) busca la función más específica. Los PC nativos se simbolizan contra el ELF exacto con `addr2line` y se agrupan por helper del dynarec (x87/div/mem), dispatch, shim, render/audio o runtime.

El timer acumula CPU del hilo guest; la ventana de inclusión se limita con tiempo monotónico. No toma muestras de los hilos de audio u otros workers. Los porcentajes son distribución de muestras, no ciclos ni IPC. El backend es Linux/QEMU y no se compila en Vita; VitaGL no corre en QEMU. La categoría de render aquí corresponde a DirectDraw software y al presentador PNG de las capturas, así que no debe leerse como costo VitaGL.

Build exacto: `hotwheels/out/hw051-pc-build/hotwheels-host`; mapa: `hotwheels/out/hw051-functions.csv`. Menú: ventana 10 s desde 3 s, corrida 15 s, 1.190 frames/44 PNG. Carrera: ventana 10 s desde 48 s, corrida 65 s con `tools/race_enter.script`, 2.500 frames/70 PNG; escena de pista ejecutada por el input guionado. Ambas cerraron con `missing=0`; no hubo otra corrida QEMU simultánea. Invocación: `HW_PC_PROFILE=1 HW_PC_PROFILE_SCENE=menu|race HW_PC_PROFILE_START_MS=3000|48000 HW_PC_PROFILE_DURATION_MS=10000 HW_PC_PROFILE_MAP=hotwheels/out/hw051-functions.csv`. Reporte: `python3 hotwheels/tools/pcprof_report.py <log> <ELF> --addr2line addr2line`.

## menu: 9958 muestras; PC host→dynablock→EIP guest

| Componente | Muestras | % total |
|---|---:|---:|
| Código traducido del juego | 2296 | 23.06 |
| dynarec helper | 337 | 3.38 |
| dynarec dispatch | 449 | 4.51 |
| shims | 370 | 3.72 |
| render/vitaGL | 3546 | 35.61 |
| audio | 109 | 1.09 |
| runtime/host/unknown | 2851 | 28.63 |

### Guest traducido — top 30

| # | EIP guest | Función Ghidra | Muestras | % total |
|---:|---:|---|---:|---:|
| 1 | `0x0041bb80` | `inpUpdate` | 702 | 7.05 |
| 2 | `0x00443b30` | `state_intro1` | 347 | 3.48 |
| 3 | `0x0041d5a0` | `swRasterFT3` | 238 | 2.39 |
| 4 | `0x004521c4` | `__ftol` | 212 | 2.13 |
| 5 | `0x00427890` | `FUN_00427890` | 182 | 1.83 |
| 6 | `0x00450af3` | `FUN_00450af3` | 161 | 1.62 |
| 7 | `0x0041c1e0` | `gpuDrawPacket` | 85 | 0.85 |
| 8 | `0x00422320` | `FUN_00422320` | 73 | 0.73 |
| 9 | `0x00408e20` | `FUN_00408e20` | 41 | 0.41 |
| 10 | `0x004503a0` | `WinMain` | 38 | 0.38 |
| 11 | `0x0c300000` | `sub_0C300000` | 32 | 0.32 |
| 12 | `0x0041c190` | `FUN_0041c190` | 16 | 0.16 |
| 13 | `0x00427bb0` | `FUN_00427bb0` | 14 | 0.14 |
| 14 | `0x004220e0` | `FUN_004220e0` | 12 | 0.12 |
| 15 | `0x00420d30` | `swRasterFT3Semi` | 8 | 0.08 |
| 16 | `0x00422e00` | `FUN_00422e00` | 8 | 0.08 |
| 17 | `0x00424900` | `state_menuLoop` | 7 | 0.07 |
| 18 | `0x0041c150` | `FUN_0041c150` | 6 | 0.06 |
| 19 | `0x00424f90` | `FUN_00424f90` | 6 | 0.06 |
| 20 | `0x0044ecf0` | `cheatTypeBuffer` | 6 | 0.06 |
| 21 | `0x00403d30` | `FUN_00403d30` | 5 | 0.05 |
| 22 | `0x0040fe10` | `FUN_0040fe10` | 5 | 0.05 |
| 23 | `0x00422e70` | `FUN_00422e70` | 5 | 0.05 |
| 24 | `0x004265d0` | `FUN_004265d0` | 5 | 0.05 |
| 25 | `0x00426bd0` | `FUN_00426bd0` | 5 | 0.05 |
| 26 | `0x0043e790` | `FUN_0043e790` | 5 | 0.05 |
| 27 | `0x00440ea0` | `FUN_00440ea0` | 5 | 0.05 |
| 28 | `0x0c301000` | `sub_0C301000` | 5 | 0.05 |
| 29 | `0x0041bb50` | `inpMouseLatch` | 4 | 0.04 |
| 30 | `0x00422890` | `FUN_00422890` | 4 | 0.04 |

### dynarec helper — funciones host dominantes

| Función host | Fuente | Muestras | % total |
|---|---|---:|---:|
| `arm_fist64_3` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/dynarec_arm_functions.c` | 274 | 2.75 |
| `__aeabi_idivmod` | `??:?` | 40 | 0.40 |
| `idiv32` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/emu/x86primop.c` | 20 | 0.20 |
| `x87_reset` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/dynarec_arm_functions.c` | 1 | 0.01 |
| `neoncache_get_st` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/dynarec_arm_functions.c` | 1 | 0.01 |
| `fpu_propagate_stack1` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/dynarec_arm_helper.c` | 1 | 0.01 |

### dynarec dispatch — funciones host dominantes

| Función host | Fuente | Muestras | % total |
|---|---|---:|---:|
| `DBGetBlock` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/dynablock.c` | 102 | 1.02 |
| `DynaRun` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/dynarec.c` | 88 | 0.88 |
| `internalDBGetBlock` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/dynablock.c` | 37 | 0.37 |
| `getNeedTest` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/custommem.c` | 35 | 0.35 |
| `isInHotPage` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/custommem.c` | 29 | 0.29 |
| `$a` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/arm_epilog.S` | 24 | 0.24 |
| `UpdateFlags` | `/work/d2vita/D2Vita/third_party/winx86/src/dynarec86/shim/x86run_flags.c` | 23 | 0.23 |
| `isprotectedDB` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/custommem.c` | 18 | 0.18 |
| `$a` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/arm_prolog.S` | 16 | 0.16 |
| `dyn86_link_direct` | `/work/d2vita/D2Vita/third_party/winx86/src/dynarec86/dyn86.c` | 15 | 0.15 |
| `rb_get_end` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/tools/rbtree.c` | 14 | 0.14 |
| `dyn86_take_break` | `/work/d2vita/D2Vita/third_party/winx86/src/dynarec86/dyn86.c` | 12 | 0.12 |

### shims — funciones host dominantes

| Función host | Fuente | Muestras | % total |
|---|---|---:|---:|
| `winx86::dx::dinput::Device::get_device_state(unsigned int, void*)` | `/work/hotwheels/src/dinput/../../../winx86-dx/src/dinput/dinput.cpp` | 197 | 1.98 |
| `hw::integration::(anonymous namespace)::Adapter::call_dev(winx86::dx::dinput::Device&, unsigned int, d2rt::Cpu&)` | `/work/hotwheels/src/integration/dinput_bridge.cpp` | 74 | 0.74 |
| `hw::integration::WaypointInput::poll()` | `/work/hotwheels/src/integration/scripted_input.cpp` | 17 | 0.17 |
| `hw::integration::RacePilotInput::poll()` | `/work/hotwheels/src/integration/scripted_input.cpp` | 16 | 0.16 |
| `hw::shims::(anonymous namespace)::D2rtGuestCpu::arg(unsigned int) const` | `/work/hotwheels/src/shims/shims.cpp` | 15 | 0.15 |
| `winx86::dx::dinput::DirectInput::refresh()` | `/work/hotwheels/src/dinput/../../../winx86-dx/src/dinput/dinput.cpp` | 14 | 0.14 |
| `winx86::dx::shims::GuestClock::ms()` | `/work/hotwheels/src/shims/../../../winx86-dx/src/shims/base.cpp` | 5 | 0.05 |
| `std::_Function_handler<unsigned int (winx86::dx::shims::GuestCpu&), winx86::dx::shims::build_base(std::shared_ptr<winx86::dx::shims::BaseContext> const&, std::function<void (winx86::dx::shims::ShimSpec)> const&)::{lambda(winx86::dx::shims::GuestCpu&)#41}>::_M_invoke(std::_Any_data const&, winx86::dx::shims::GuestCpu&)` | `/work/hotwheels/src/shims/../../../winx86-dx/src/shims/imports.cpp` | 5 | 0.05 |
| `valid` | `/work/hotwheels/src/integration/dinput_bridge.cpp` | 5 | 0.05 |
| `std::_Function_handler<unsigned int (winx86::dx::shims::GuestCpu&), winx86::dx::shims::build_base(std::shared_ptr<winx86::dx::shims::BaseContext> const&, std::function<void (winx86::dx::shims::ShimSpec)> const&)::{lambda(winx86::dx::shims::GuestCpu&)#20}>::_M_invoke(std::_Any_data const&, winx86::dx::shims::GuestCpu&)` | `/work/hotwheels/src/shims/../../../winx86-dx/src/shims/imports.cpp` | 4 | 0.04 |
| `std::_Function_handler<unsigned long long (), hw::shims::install(d2rt::Bridge&, d2rt::Cpu&, hw::shims::Context&)::{lambda()#2}>::_M_invoke(std::_Any_data const&)` | `/work/hotwheels/src/shims/install.cpp` | 4 | 0.04 |
| `std::_Function_handler<unsigned int (d2rt::Cpu&), hw::integration::(anonymous namespace)::Adapter::setup()::{lambda(bool, std::vector<unsigned char, std::allocator<unsigned char> > const&)#1}::operator()(bool, std::vector<unsigned char, std::allocator<unsigned char> > const&) const::{lambda(d2rt::Cpu&)#1}>::_M_invoke(std::_Any_data const&, d2rt::Cpu&)` | `/work/hotwheels/src/integration/dinput_bridge.cpp` | 3 | 0.03 |

### render/vitaGL — funciones host dominantes

| Función host | Fuente | Muestras | % total |
|---|---|---:|---:|
| `hw::ddraw::PngPresenter::present(unsigned char const*, unsigned int, unsigned int const*)` | `/work/hotwheels/src/ddraw/png.cpp` | 2326 | 23.36 |
| `hw::ddraw::(anonymous namespace)::crc_update(unsigned int, unsigned char const*, unsigned int)` | `/work/hotwheels/src/ddraw/png.cpp` | 635 | 6.38 |
| `winx86::dx::ddraw::Device::blt_fast(winx86::dx::ddraw::Surface&, unsigned int, unsigned int, winx86::dx::ddraw::Surface&, winx86::dx::ddraw::Rect, unsigned int)` | `/work/hotwheels/src/ddraw/../../../winx86-dx/src/ddraw/software.cpp` | 579 | 5.81 |
| `adler32` | `/work/hotwheels/src/ddraw/png.cpp` | 3 | 0.03 |
| `winx86::dx::ddraw::Device::unlock(winx86::dx::ddraw::Surface&)` | `/work/hotwheels/src/ddraw/../../../winx86-dx/src/ddraw/software.cpp` | 1 | 0.01 |
| `winx86::dx::ddraw::Device::flip(winx86::dx::ddraw::Surface&)` | `/work/hotwheels/src/ddraw/../../../winx86-dx/src/ddraw/software.cpp` | 1 | 0.01 |
| `winx86::dx::ddraw::Device::set_entries(unsigned int, winx86::dx::ddraw::PaletteEntry const*, unsigned int)` | `/work/hotwheels/src/ddraw/../../../winx86-dx/src/ddraw/software.cpp` | 1 | 0.01 |

### audio — funciones host dominantes

| Función host | Fuente | Muestras | % total |
|---|---|---:|---:|
| `winx86::dx::audio::Mixer::mix(short*, int)` | `/work/hotwheels/src/audio/../../../winx86-dx/src/audio/mixer.cpp` | 74 | 0.74 |
| `winx86::dx::audio::Mixer::sample(winx86::dx::audio::Mixer::SourceState const&, int) const` | `/work/hotwheels/src/audio/../../../winx86-dx/src/audio/mixer.cpp` | 14 | 0.14 |
| `d2rt::dsound::(anonymous namespace)::mix_grain(d2rt::Cpu*, int)` | `/work/d2vita/D2Vita/third_party/winx86/src/runtime/ds_emul.cpp` | 6 | 0.06 |
| `non-virtual thunk to hw::audio::MusicPlayer::rate() const` | `/work/hotwheels/src/audio/music.h` | 5 | 0.05 |
| `winx86::dx::audio::DsoundBus::push(short const*, int, int)` | `/work/hotwheels/src/audio/../../../winx86-dx/src/audio/dsound_bus.cpp` | 2 | 0.02 |
| `__gnu_cxx::__normal_iterator<winx86::dx::audio::Mixer::SourceState*, std::vector<winx86::dx::audio::Mixer::SourceState, std::allocator<winx86::dx::audio::Mixer::SourceState> > >::__normal_iterator(winx86::dx::audio::Mixer::SourceState* const&)` | `/usr/arm-linux-gnueabihf/include/c++/13/bits/stl_iterator.h` | 2 | 0.02 |
| `hw::audio::WavFileSource::read(short*, int)` | `/work/hotwheels/src/audio/wav.cpp` | 2 | 0.02 |
| `hw::audio::ds_backend_feed(short const*, int, int)` | `/work/hotwheels/src/audio/ds_bridge.cpp` | 1 | 0.01 |
| `winx86::dx::audio::DsoundBus::rate() const` | `/work/hotwheels/src/audio/../../../winx86-dx/src/audio/dsound_bus.cpp` | 1 | 0.01 |
| `winx86::dx::audio::clamp_s16(int)` | `/work/hotwheels/src/audio/../../../winx86-dx/src/audio/../../include/winx86-dx/audio/pcm.h` | 1 | 0.01 |
| `std::__atomic_base<d2rt::audio::Sink*>::load(std::memory_order) const` | `/usr/arm-linux-gnueabihf/include/c++/13/bits/atomic_base.h` | 1 | 0.01 |

> vitaGL es Vita-only y no ejecuta en qemu-arm; cero muestras en ese componente no valida Vita.

## Desglose heurístico de runtime/host/unknown — carrera QEMU

La cifra histórica de 41,50 % no conserva su log PC crudo, así que no se puede
reclasificar muestra por muestra. Para identificar la causa repetí la
simbolización sobre la captura persistida `out/opus-wt-165209633650874/run.log`
(4.871 muestras, ventana 22–37 s) con el ELF `out/hw051-pc-build/hotwheels-host`
(SHA-256 `cf30a22825459e73801002ac5a0ea687568a76b508624fa85994c9efc40555de`).
Este muestreo dio 1.870/4.871 (38,39 %) en unknown; no reemplaza la cifra
histórica ni una captura Vita.

| Clasificación de unknown | Muestras | % total | % unknown |
|---|---:|---:|---:|
| C++ runtime/containers | 558 | 11,46 | 29,8 |
| accesores `d2rt::Cpu` | 548 | 11,25 | 29,3 |
| libc/memoria | 310 | 6,36 | 16,6 |
| libm/soft-float | 239 | 4,91 | 12,8 |
| otro runtime | 167 | 3,43 | 8,9 |
| locks/scheduler | 28 | 0,57 | 1,5 |
| PC sin símbolo | 20 | 0,41 | 1,1 |

El clasificador usa heurísticas de nombre de símbolo/fuente y requiere revisar
coincidencias; las funciones dominantes incluyen `Cpu::arg()` (332 muestras),
`std::ios_base::failure` (256), `cosl` (239), `Cpu::write_u32()` (153),
destructor de `std::function` (104), `__calloc` (66), `Cpu::read_u32()` (57) y
`realloc` (21). No apareció `memcpy` como función dominante. Las 28 muestras
CPU dentro de símbolos de locks/scheduler no miden espera bloqueada; este sampler
tampoco mide el overhead de su handler, porque registra el PC interrumpido.
En el PC ARM que QEMU user-mode entrega al handler, el programa muestreado es
el ELF de Hot Wheels: el proceso QEMU/TCG y el kernel host no aparecen como
símbolos de este binario ni se pueden atribuir a partir de estas muestras. En
QEMU el componente render/VitaGL fue 7 muestras de `RefRaster`; VitaGL no corre
en ese backend.

El reporte ahora incluye este desglose para capturas siguientes. El dato
pendiente para afirmar que explica el 41,50 % es repetir la ventana con el ELF
y build de esa captura original; conservar sólo el resumen impide resolverlo
retroactivamente.

## race: 9944 muestras; PC host→dynablock→EIP guest

| Componente | Muestras | % total |
|---|---:|---:|
| Código traducido del juego | 2701 | 27.16 |
| dynarec helper | 865 | 8.70 |
| dynarec dispatch | 954 | 9.59 |
| shims | 499 | 5.02 |
| render/vitaGL | 493 | 4.96 |
| audio | 305 | 3.07 |
| runtime/host/unknown | 4127 | 41.50 |

### Guest traducido — top 30

| # | EIP guest | Función Ghidra | Muestras | % total |
|---:|---:|---|---:|---:|
| 1 | `0x004521c4` | `__ftol` | 603 | 6.06 |
| 2 | `0x00450af3` | `FUN_00450af3` | 417 | 4.19 |
| 3 | `0x0041d5a0` | `swRasterFT3` | 368 | 3.70 |
| 4 | `0x00412df0` | `timerWaitFrame40Hz` | 301 | 3.03 |
| 5 | `0x00422320` | `FUN_00422320` | 193 | 1.94 |
| 6 | `0x0041c1e0` | `gpuDrawPacket` | 108 | 1.09 |
| 7 | `0x0c300000` | `sub_0C300000` | 75 | 0.75 |
| 8 | `0x004073c0` | `FUN_004073c0` | 65 | 0.65 |
| 9 | `0x00450c19` | `FUN_00450c19` | 61 | 0.61 |
| 10 | `0x004220e0` | `FUN_004220e0` | 57 | 0.57 |
| 11 | `0x00420d30` | `swRasterFT3Semi` | 54 | 0.54 |
| 12 | `0x0040d360` | `FUN_0040d360` | 36 | 0.36 |
| 13 | `0x0040dd30` | `FUN_0040dd30` | 31 | 0.31 |
| 14 | `0x00436e30` | `FUN_00436e30` | 22 | 0.22 |
| 15 | `0x00434fb0` | `FUN_00434fb0` | 18 | 0.18 |
| 16 | `0x0043ed00` | `FUN_0043ed00` | 16 | 0.16 |
| 17 | `0x0040cd70` | `FUN_0040cd70` | 14 | 0.14 |
| 18 | `0x00432090` | `FUN_00432090` | 14 | 0.14 |
| 19 | `0x00434d40` | `FUN_00434d40` | 13 | 0.13 |
| 20 | `0x00419740` | `FUN_00419740` | 11 | 0.11 |
| 21 | `0x004225b0` | `FUN_004225b0` | 11 | 0.11 |
| 22 | `0x00422710` | `FUN_00422710` | 11 | 0.11 |
| 23 | `0x00434120` | `FUN_00434120` | 10 | 0.10 |
| 24 | `0x00418900` | `FUN_00418900` | 9 | 0.09 |
| 25 | `0x00440ea0` | `FUN_00440ea0` | 9 | 0.09 |
| 26 | `0x0044f400` | `FUN_0044f400` | 9 | 0.09 |
| 27 | `0x0041c190` | `FUN_0041c190` | 8 | 0.08 |
| 28 | `0x00427d30` | `FUN_00427d30` | 8 | 0.08 |
| 29 | `0x00422e00` | `FUN_00422e00` | 7 | 0.07 |
| 30 | `0x0043d9c0` | `FUN_0043d9c0` | 7 | 0.07 |

### dynarec helper — funciones host dominantes

| Función host | Fuente | Muestras | % total |
|---|---|---:|---:|
| `arm_fist64_3` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/dynarec_arm_functions.c` | 589 | 5.92 |
| `__aeabi_idivmod` | `??:?` | 172 | 1.73 |
| `idiv32` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/emu/x86primop.c` | 104 | 1.05 |

### dynarec dispatch — funciones host dominantes

| Función host | Fuente | Muestras | % total |
|---|---|---:|---:|
| `DBGetBlock` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/dynablock.c` | 237 | 2.38 |
| `isprotectedDB` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/custommem.c` | 136 | 1.37 |
| `rb_get_end` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/tools/rbtree.c` | 131 | 1.32 |
| `DynaRun` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/dynarec.c` | 110 | 1.11 |
| `internalDBGetBlock` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/dynablock.c` | 68 | 0.68 |
| `$a` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/arm_epilog.S` | 51 | 0.51 |
| `getNeedTest` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/custommem.c` | 44 | 0.44 |
| `isInHotPage` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/custommem.c` | 43 | 0.43 |
| `UpdateFlags` | `/work/d2vita/D2Vita/third_party/winx86/src/dynarec86/shim/x86run_flags.c` | 33 | 0.33 |
| `dyn86_link_direct` | `/work/d2vita/D2Vita/third_party/winx86/src/dynarec86/dyn86.c` | 29 | 0.29 |
| `$a` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/dynarec/arm_prolog.S` | 27 | 0.27 |
| `getDB` | `/work/d2vita/D2Vita/third_party/winx86/third_party/box86-dynarec/custommem.c` | 23 | 0.23 |

### shims — funciones host dominantes

| Función host | Fuente | Muestras | % total |
|---|---|---:|---:|
| `winx86::dx::shims::build_base(std::shared_ptr<winx86::dx::shims::BaseContext> const&, std::function<void (winx86::dx::shims::ShimSpec)> const&)::{lambda(winx86::dx::shims::GuestCpu&)#19}::operator()(winx86::dx::shims::GuestCpu&) const` | `/work/hotwheels/src/shims/../../../winx86-dx/src/shims/imports.cpp` | 147 | 1.48 |
| `winx86::dx::shims::GuestClock::take_stats(winx86::dx::shims::GuestClock::Stats&)` | `/work/hotwheels/src/shims/../../../winx86-dx/src/shims/base.cpp` | 83 | 0.83 |
| `hw::shims::(anonymous namespace)::D2rtGuestCpu::arg(unsigned int) const` | `/work/hotwheels/src/shims/shims.cpp` | 66 | 0.66 |
| `std::_Function_handler<unsigned long long (), hw::shims::install(d2rt::Bridge&, d2rt::Cpu&, hw::shims::Context&)::{lambda()#2}>::_M_invoke(std::_Any_data const&)` | `/work/hotwheels/src/shims/install.cpp` | 50 | 0.50 |
| `winx86::dx::shims::GuestClock::qpc()` | `/work/hotwheels/src/shims/../../../winx86-dx/src/shims/base.cpp` | 48 | 0.48 |
| `winx86::dx::shims::(anonymous namespace)::write_at(winx86::dx::shims::GuestCpu&, unsigned int, unsigned long long, void const*, unsigned int)` | `/work/hotwheels/src/shims/../../../winx86-dx/src/shims/imports.cpp` | 36 | 0.36 |
| `winx86::dx::shims::GuestClock::now()` | `/work/hotwheels/src/shims/../../../winx86-dx/src/shims/base.cpp` | 30 | 0.30 |
| `hw::shims::(anonymous namespace)::D2rtGuestCpu::write(unsigned int, void const*, unsigned int)` | `/work/hotwheels/src/shims/shims.cpp` | 29 | 0.29 |
| `winx86::dx::dinput::Device::get_device_state(unsigned int, void*)` | `/work/hotwheels/src/dinput/../../../winx86-dx/src/dinput/dinput.cpp` | 2 | 0.02 |
| `hw::integration::(anonymous namespace)::Adapter::call_dev(winx86::dx::dinput::Device&, unsigned int, d2rt::Cpu&)` | `/work/hotwheels/src/integration/dinput_bridge.cpp` | 2 | 0.02 |
| `hw::ddraw::(anonymous namespace)::GuestMem::ptr(unsigned int, unsigned int)` | `/work/hotwheels/src/ddraw/bridge.cpp` | 1 | 0.01 |
| `hw::ddraw::(anonymous namespace)::Adapter::fill_desc(unsigned int, winx86::dx::ddraw::Surface const&, unsigned int) [clone .part.0]` | `/work/hotwheels/src/ddraw/bridge.cpp` | 1 | 0.01 |

### render/vitaGL — funciones host dominantes

| Función host | Fuente | Muestras | % total |
|---|---|---:|---:|
| `hw::ddraw::(anonymous namespace)::crc_update(unsigned int, unsigned char const*, unsigned int)` | `/work/hotwheels/src/ddraw/png.cpp` | 285 | 2.87 |
| `winx86::dx::ddraw::Device::blt_fast(winx86::dx::ddraw::Surface&, unsigned int, unsigned int, winx86::dx::ddraw::Surface&, winx86::dx::ddraw::Rect, unsigned int)` | `/work/hotwheels/src/ddraw/../../../winx86-dx/src/ddraw/software.cpp` | 155 | 1.56 |
| `hw::ddraw::PngPresenter::present(unsigned char const*, unsigned int, unsigned int const*)` | `/work/hotwheels/src/ddraw/png.cpp` | 53 | 0.53 |

### audio — funciones host dominantes

| Función host | Fuente | Muestras | % total |
|---|---|---:|---:|
| `winx86::dx::audio::Mixer::mix(short*, int)` | `/work/hotwheels/src/audio/../../../winx86-dx/src/audio/mixer.cpp` | 190 | 1.91 |
| `winx86::dx::audio::DsoundBus::rate() const` | `/work/hotwheels/src/audio/../../../winx86-dx/src/audio/dsound_bus.cpp` | 32 | 0.32 |
| `non-virtual thunk to hw::audio::MusicPlayer::rate() const` | `/work/hotwheels/src/audio/music.h` | 28 | 0.28 |
| `winx86::dx::audio::Mixer::sample(winx86::dx::audio::Mixer::SourceState const&, int) const` | `/work/hotwheels/src/audio/../../../winx86-dx/src/audio/mixer.cpp` | 22 | 0.22 |
| `hw::audio::WavFileSource::read(short*, int)` | `/work/hotwheels/src/audio/wav.cpp` | 6 | 0.06 |
| `winx86::dx::audio::DsoundBus::read(short*, int)` | `/work/hotwheels/src/audio/../../../winx86-dx/src/audio/dsound_bus.cpp` | 4 | 0.04 |
| `d2rt::dsound::(anonymous namespace)::mix_grain(d2rt::Cpu*, int)` | `/work/d2vita/D2Vita/third_party/winx86/src/runtime/ds_emul.cpp` | 4 | 0.04 |
| `d2rt::dsound::(anonymous namespace)::recompute_gains(d2rt::dsound::(anonymous namespace)::Voice&)` | `/work/d2vita/D2Vita/third_party/winx86/src/runtime/ds_emul.cpp` | 3 | 0.03 |
| `__gnu_cxx::__normal_iterator<winx86::dx::audio::Mixer::SourceState*, std::vector<winx86::dx::audio::Mixer::SourceState, std::allocator<winx86::dx::audio::Mixer::SourceState> > >::__normal_iterator(winx86::dx::audio::Mixer::SourceState* const&)` | `/usr/arm-linux-gnueabihf/include/c++/13/bits/stl_iterator.h` | 2 | 0.02 |
| `d2rt::dsound::(anonymous namespace)::m_buf_play(d2rt::Cpu&)` | `/work/d2vita/D2Vita/third_party/winx86/src/runtime/ds_emul.cpp` | 2 | 0.02 |
| `d2rt::dsound::(anonymous namespace)::m_buf_setpan(d2rt::Cpu&)` | `/work/d2vita/D2Vita/third_party/winx86/src/runtime/ds_emul.cpp` | 2 | 0.02 |
| `winx86::dx::audio::clamp_s16(int)` | `/work/hotwheels/src/audio/../../../winx86-dx/src/audio/../../include/winx86-dx/audio/pcm.h` | 2 | 0.02 |

> vitaGL es Vita-only y no ejecuta en qemu-arm; cero muestras en ese componente no valida Vita.
