# hello-port

`hello_guest.c` is a PE32/i386 executable compiled by llvm-mingw. Its entry point calls imports from `ddraw.dll`, `dsound.dll` and `dinput.dll`. `hello_runner.cpp` loads the executable with the real `winx86::Bridge`; its small per-port import adapters call the portable DDraw surface/presenter, DirectSound PCM bus/mixer and DirectInput snapshot/mapping APIs.

Run the cross-architecture smoke from the monorepo root:

```sh
winx86-dx/template/run_qemu_smoke.sh
```

The script builds the Win32 guest, builds the ARM host runner against `libwinx86.a`, and executes the guest under qemu-arm with a 30-second timeout. PASS requires zero unresolved imports, successful return from the PE entry, and each of the three portable cores to pass its small observable check.

This is a wiring template, not a drop-in implementation of the DirectX COM ABI: the test adapters intentionally accept null output pointers and exercise the cores directly. A real port must marshal and validate each guest pointer/structure and implement the COM calls that its executable uses. Move the sample's tiny display/input/audio choices into a per-game profile when extending it.
