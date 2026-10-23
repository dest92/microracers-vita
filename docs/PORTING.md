# Starting a Win32/x86 game port

Use this guide for a new PE32/i386 game port in `vita-ports`. The example under [winx86-dx/template](../winx86-dx/template/) is the small executable reference; it does not contain game assets.

## Minimal project layout

Keep the game profile and device adapters in the port. Keep portable API behavior in `winx86-dx` and x86 execution/loading in `d2vita/D2Vita/third_party/winx86`.

```text
<port>/
  specs/                 # observable contract and acceptance criteria
  src/host/              # PE loading, Bridge setup, import adapters
  src/platform/          # Vita input/audio/video, clocks and file mounts
  config/game_profile.h  # paths, modes, button map, rates, known quirks
  tests/                 # host tests and a small PE guest for qemu
  tools/build_vita.sh    # explicit sources, Vita includes and linker inputs
```

Start with one PE32/i386 executable and a minimal profile. Do not copy a game's DLLs or assets into the generic library. Keep executable names, base addresses, display modes, key/button mappings, mount paths, CD tracks, audio rates, memory sizing and known workarounds in that port's profile.

## What the libraries provide

`winx86` provides the PE32 loader, guest address space, x86 CPU/dynarec and `Bridge` for import registration. `winx86-dx` provides reusable host-side cores:

- `ddraw`: surfaces, software blits and palette operations.
- `d3d`: device state and typed commands/textures; the renderer remains with the port.
- `dinput`: DirectInput device model and a configurable map from host pad snapshots to guest state.
- `audio`: PCM interfaces, mixer/resampling and a bounded DirectSound bus.
- `winmm`: MCI/MMIO models over injected file and music interfaces.
- `io`: Win32-path VFS and buffered/blob readers.
- `shims`: reusable base service models for guest memory, files, time, messages, CD metadata, logging and host callbacks.

These are cores, not complete guest DLL exports. The port registers each required PE import with `Bridge`, marshals 32-bit guest addresses safely, and calls the core. Implement only the guest ABI surface that the executable imports. Add new behavior to `winx86-dx` only when its contract has no game-specific paths, identity, defaults or device policy.

## Per-game configuration and ownership

Create a value configuration in `config/game_profile.h` and pass it into the port adapters at startup. At minimum define:

- executable identity and PE layout policy;
- mounted game/data/save paths and CD identity/catalog;
- supported screen modes, output scaling and presentation policy;
- DirectInput button map, deadzone and player selection;
- audio output rate, gains, priming/capacity and missing-track behavior;
- explicit quirks justified by a test or measured trace.

The port owns host devices, platform threads, rendering backend and PE import thunks. Generic objects own the C++ state they create and document. References such as `InputSource`, `Presenter`, `CommandSink`, texture/file callbacks and PCM sources are borrowed; keep them alive for the documented object's lifetime. Guest memory remains owned by the guest allocator/runtime. Serialize stateful non-thread-safe cores on the guest thread; `DsoundBus` is the bounded producer/consumer exception. Do not assume x86 TSO on ARMv7: use the C++ mutex/condition-variable contract and the runtime's required D-cache/I-cache maintenance for generated code.

## Build consumption

For a host CMake consumer, add the library and link the target:

```cmake
add_subdirectory("${CMAKE_SOURCE_DIR}/../winx86-dx" winx86_dx)
target_link_libraries(port_host PRIVATE winx86_dx)
```

For Vita, add `winx86-dx/include` to the include path and each needed `winx86-dx/src/<module>/*.cpp` exactly once to the port's explicit source list. Link the Vita `winx86` archive and platform libraries as well. Keep the source list explicit so a module cannot be accidentally omitted or compiled twice. Build the guest `.exe` as PE32/i386 with llvm-mingw; the Vita executable is an ARMv7 ELF and is a separate artifact.

## Test progression

1. Write the port spec and host tests for each observable import contract before wiring it to the game.
2. Run `cmake -S hotwheels -B hotwheels/build`, build serially, and use CTest plus the module runners. For standalone core-only work, also run the `winx86-dx` CMake target.
3. Add a small PE guest compiled with llvm-mingw that imports the APIs under test. Run it through the real `winx86::Bridge` and ARM runtime under qemu with `winx86-dx/template/run_qemu_smoke.sh` as the minimal reference.
4. Build the port's Vita ELF/VPK with its own toolchain script. Only then move to emulator and hardware observation for features that need a rendering, audio or controller backend.

The qemu template exercises a deliberately small import set and portable API paths. It is a wiring smoke test, not proof of complete DirectX compatibility or device behavior on Vita.
