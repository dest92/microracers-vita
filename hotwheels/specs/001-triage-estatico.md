# HW-001 — Triage estático del juego original

## Problema

`game/Hot Wheels® - Micro Racers.exe` es un instalador de 2023. Analizar sólo su tabla PE confunde dependencias del instalador con las del juego de 2000.

## Contrato observable

- La extracción reproduce el MSI, CAB e INI del SFX sin ejecutar código Windows. Interpreta el directorio `ADVINSTSFX`, respeta offsets/tamaños y aplica XOR `0xFF` sólo a los primeros `0x200` bytes de las entradas marcadas.
- El ejecutable de juego instalado por el repack y el extraído de `HWMR.iso/data1.cab` coinciden por SHA-256.
- `docs/triage.md` enumera todos los imports del PE del juego por DLL y distingue los del instalador; registra gráficos, sonido, entrada, CD/DRM, red, threads y memoria con nivel de evidencia.
- Todo payload extraído queda bajo `hotwheels/extracted/`, ignorado por Git.

## Criterios de aceptación

1. Un fixture sintético valida límites de entradas y de la zona XOR, incluida una entrada sin XOR.
2. MSI y CAB extraídos abren con `7z l`; ISO abre como ISO 9660; `unshield l data1.cab` enumera `HotWheels.exe`.
3. El informe identifica el binario exacto por SHA-256 y mide todos los tamaños expresados.
4. `git diff --cached --name-only` no contiene ejecutables, archivos de audio, assets ni volcados.

## No objetivos

Ejecución en Wine/Vita, comprobación de llamadas efectivamente alcanzadas, presupuesto real de RAM o implementar shims.
