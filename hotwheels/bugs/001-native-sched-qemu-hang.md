# BUG 001 — scheduler nativo se cuelga en qemu-arm tras el 1.er CreateFileA fallido
Nivel: qemu-arm (hotwheels-host). Repro: `HW_ENV="TRAPTAG=1" hotwheels/tools/run_host.sh 8`
Síntoma: 45 trampas (la última `GetLastError` -> 2, después de `CreateFileA("data\menu\highscore.dat")`), después ninguna.
`dump_threads`: thr 1 st=2 (Running) con eip=0x0c3ffff0 (= centinela del Bridge) y el GIL ocupado.
Con `HW_SCHED=coop` el MISMO binario sigue de largo (más de 100 trampas). Descartado: argc de los shims (los de HW-006 y los genéricos de winx86 coinciden con mingw @N).
Hipótesis abiertas: interacción entre el trap/redirect del centinela y NativeScheduler bajo qemu; probar `WX86_NOCANCEL=1`, `WX86_YIELD`, y ver si en Vita3K pasa lo mismo.

## Causa raíz (fixer, 2026-10-03) — VERIFICADA
La trampa 46 (vista con `HW_SCHED=coop`) es `DDRAW.dll!DirectDrawEnumerateExA`, cuyo shim invoca un
callback del invitado con `Bridge::call_va` → `cpu->run()` ANIDADO, con el GIL ya tomado por el
`TrapGuard` de la trampa (cpu_box86.cpp:1487). El GIL de winx86 es NO recursivo (gil.h:14): la
primera trampa del callback — al menos el retorno al centinela 0x0c3ffff0 — vuelve a `gil::lock()`
en el mismo hilo → auto-deadlock. De ahí `EIP=centinela`, `GIL busy`, `sl=0`. No es qemu: pasa
igual en Vita (mismo código). Coop no lo ve porque ahí `gil::active()==false`.
Lo mismo aplica a todo `call_va` desde un shim (WndProc de `shims.cpp`, EnumDevices de DInput,
EnumSurfaces/EnumModes de DDraw).

## Fix
`hotwheels/src/shims/guest_call.h`: `hw::guest_call()` = `gil::Release` + `call_va` (no-op en coop).
Usado en `shims/install.cpp` (call_guest), `ddraw/bridge.cpp` (invoke_callback) y
`integration/dinput_bridge.cpp`. Sin parche a winx86.
Resultado (qemu-arm, nativo, 10 s): >4000 trampas, 63 frames, menú en PNG, apagado limpio.
Regresión: `hotwheels/tests/run_bug001.sh` (exe real, nativo) → `BUG-001: OK`.

## Riesgo residual (no bloquea)
`call_va` usa SIEMPRE la pila del Bridge (`stack_base_+size-0x1000`), no la del hilo actual. Con el
GIL liberado, dos hilos invitados con callbacks anidados simultáneos compartirían esa pila. Hoy
sólo el main hace callbacks; si aparece un 2.º, parche aditivo a winx86 para usar el ESP del hilo.
