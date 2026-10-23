# winx86-dx

Portable host-side cores for Win32 multimedia and system APIs used by PE32/i386 ports. The library sits next to the generic `winx86` runtime: `winx86` loads and executes the guest; this library supplies reusable DirectX, input, audio, file and base-service behavior. A game port owns the PE import bridge, platform devices, rendering backend and per-game policy.

Start with [the porting guide](../docs/PORTING.md). The runnable minimal consumer is [template/](template/); `template/run_qemu_smoke.sh` compiles its own PE32 test program with llvm-mingw, loads it through `winx86::Bridge`, and runs it under qemu-arm.

## Modules

Public headers live in `include/winx86-dx/`; implementations live in `src/`.

| Module | Reusable behavior | Port-owned behavior |
|---|---|---|
| `ddraw` | Surface storage, palette, clipping and software blits through guest-memory and presenter interfaces | PE32 COM thunks, screen modes, presentation and scaling |
| `d3d` | D3D6-style device state, texture snapshots and typed command sink | Guest ABI bridge, command wire/renderer, shaders and graphics backend |
| `dinput` | DirectInput device model, state snapshots and configurable pad mapping | Vita/host input polling, player policy and the PE32 COM bridge |
| `audio` | PCM source, mixer/resampling and bounded DirectSound producer/consumer bus | Guest DirectSound thunks, audio-device output, pacing and game-specific gain/rate |
| `winmm` | MCI/MMIO values and operations with injected file/music interfaces | Guest structure marshalling, CD catalog, paths and music assets |
| `io` | Win32-path VFS and buffered or immutable-blob read streams | Mount roots, save policy, cache limits and host filesystem |
| `shims` | Base services, clock/message/CD models, import descriptions and injected operations | Values such as executable identity, volume identity, DLL list, time source and scheduler callbacks |

This is a library of portable cores and contracts, not a prebuilt set of complete Windows DLL replacements. A port registers guest imports with `winx86::Bridge`, validates/copies PE32 memory through `Cpu`, and adapts those imports to the public C++ interfaces. Keep guest addresses as 32-bit values; never reinterpret them as host pointers.

## Build and tests

Host library and regression tests:

```sh
cmake -S winx86-dx -B build/winx86-dx
cmake --build build/winx86-dx
```

The monorepo's integration tests are also registered by `hotwheels/CMakeLists.txt`:

```sh
cmake -S hotwheels -B hotwheels/build
cmake --build hotwheels/build -j1
ctest --test-dir hotwheels/build --output-on-failure -R '^winx86_dx_'
```

The template's qemu test is the cross-architecture integration gate:

```sh
winx86-dx/template/run_qemu_smoke.sh
```

It needs llvm-mingw's i686 compiler, Docker image `hw-host-arm:24.04`, and the monorepo's `d2vita/D2Vita/third_party/winx86`. It takes `hotwheels/out/BUILD_HOST_BUSY` while compiling/running and removes successful temporary output. Set `LLVM_MINGW_ROOT`, `LLVM_MINGW_BIN`, or `WINX86_DIR` to use non-default locations. For an actual Vita port, follow the consumer source-list steps in `docs/PORTING.md`; the generic sources are compiled with the Vita toolchain and `-Iwinx86-dx/include`.
