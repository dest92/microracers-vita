# HW-007 — integración del ejecutable real

## Problema

El ejecutable entra al CRT bajo qemu-arm, pero cualquier importación u objeto COM
sin puente guest detiene el arranque. Hace falta conectar los shims ya probados
al mismo `Bridge`, registrar la trampa alcanzada y repetir hasta el menú.

## Contrato observable

- `run_host.sh` carga el `HotWheels.exe` del usuario y supera
  `DirectInputCreateA` con un objeto `IDirectInputA` válido en memoria guest.
- `CreateDevice` de teclado/mouse devuelve vtable callable, `SetDataFormat`,
  `Acquire` y `GetDeviceState` transfieren datos a direcciones guest válidas.
- Las trampas pendientes quedan identificadas por DLL, símbolo y dirección de
  retorno; el present DDraw guarda PNG en `hotwheels/out`.
- El runtime Vita usa el mismo registro de shims y un backend present de HW-021.

## Aceptación

1. Prueba de puente DINPUT con CPU y Bridge host: objeto, slots, marshalling y
   rechazo de punteros inválidos.
2. En qemu-arm, la traza supera `DirectInputCreateA` y el siguiente hito queda
   registrado en `docs/boot-log.md`.
3. El smoke ejecuta tests host, build Vita, qemu-arm y Vita3K; el resumen y la
   evidencia del menú se conservan en `hotwheels/out`.

## No objetivos

El backend de carrera 3D y la medición en consola pertenecen a otras features.

## Extensión Vita3K (2026-10-03)

`runtime_vita` registra `install_all` antes de link y enruta cada Flip al
`VitaPresenter` con ancho/alto de la primaria, inicialización diferida y resize.
El build incluye shims/input/audio/WINMM/present; los datos se stagean aparte del
VPK. Aceptación M3 Vita: boot.log registra frames y una captura del harness
muestra el menú real. Un frame contado sin imagen visible no satisface M3.

## Validación consola autorizada (2026-10-03)

Deploy exclusivo `ux0:app/HWMR00002` y `ux0:data/hotwheels`: quit all,
subida .new, tamaño FTP idéntico al local, rename, launch. Rechazar TITLEID
ocupado por otra app; preservar saves existentes. Si VitaCompanion no registra
la app copiada, dejar VPK para instalación VitaShell manual y seguir emulador.
El runtime escribe `hotwheels.log` por IO Vita directo como señal del watchdog.
No tocar plugins, configuración tai ni apps/saves ajenos. Dumps core excluidos.
