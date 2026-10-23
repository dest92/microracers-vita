# HW-026 — boot de carrera bajo qemu-arm

## Problema
El boot llega al menú; falta navegarlo de forma reproducible y ejercitar la
carrera real. Wine verificó cuatro Return: jugador, bandera/carrera, pista,
coche. La carrera software y la D3D6 requieren evidencias separadas.

## Contrato observable
- `HW_INPUT_SCRIPT` selecciona un InputSource DINPUT portable, sin alterar el
  input Vita. Cada línea es `frame DIK duración` (frame/duración decimales,
  DIK decimal o hexadecimal), con comentarios `#`.
- Estado de teclas depende de frames presentados, no de cantidad de polls ni
  velocidad del host. Rango [frame,frame+duración); permite teclas simultáneas.
- Archivo inválido/fuera de rango falla antes del boot con línea y motivo.
- Default sin guion conserva el input anterior.
- PNG del exe real muestra una carrera y se registra su modo/resolución.
- Puente D3D6 HW-010 se registra en boot con CreateDevice real y un backend
  host; evidencia de carrera D3D6 separada de carrera software.

## Aceptación
1. Test rojo portable de bordes press/release, polls repetidos, combos y errores.
2. qemu-arm nativo con guion navega desde boot al circuito, PNG inspeccionado.
3. Build/host tests verdes y review; log sin imports faltantes nuevos ocultos.
4. Carrera D3D6 real observada (CreateDevice/draws), con puente de render.

## No objetivos
Automatizar todas las pantallas, gamepad físico, interpolación 60Hz y soak.
