# SPEC-050 — RE asistida (REA + Ghidra 12.1.4)
- Feature: HW-071 · Milestone: M8 · Dueño: rea-re · Estado: en curso

## Problema
HW-067..HW-070 (oráculo de render por pista, fixes de BUG-047, oráculo de audio DSound, nitidez) necesitan
hechos del binario que no están en `docs/re-engine.md` (Ghidra 11.3.2): qué distingue el render de las pistas
no-playa, y el modelo de mezcla de audio. La infraestructura de RE del repo (proyecto Ghidra 11, `Dump.py`)
no aplica a la consola de REA/Ghidra 12.1.4.

## Contrato observable
REA MCP (`rea` 6.7.0) con proveedor Ghidra 12.1.4 (`GHIDRA_INSTALL_DIR=/home/user/opt/ghidra_12.1.4_PUBLIC`)
abre `hotwheels/extracted/original/Program_Files/HotWheels.exe` (SHA-256 `5418a6aa…1122`) y devuelve pseudocódigo
de cualquier dirección del PE32/i386 en la base `0x400000`. Los pedidos de RE de otros agentes se atienden por
`coordination/rea-re.md` (sección "Pedidos") y se responden ahí mismo. Los resultados largos van a
`hotwheels/docs/re-*.md` con el nivel de evidencia entre corchetes; toda afirmación cita dirección VA.

## Criterios de aceptación
- [x] CA1: sesión REA abre el exe con Ghidra 12.1.4 y el VA coincide con los registros previos (re-engine.md).
- [x] CA2: respuesta a HW-068 con la lista pedida (render states/TSS/valores, FVF/stride, primitivas, rhw/z,
      formatos/paletas, Lock/Unlock/SetPalette) y la diferencia playa vs. resto, con VA por ítem.
- [x] CA3: `docs/re-tracks.md` (selección de pista, archivos por pista, diferencias de render) y
      `docs/re-audio.md` (modelo de mezcla, formatos, SetFrequency/Volume/Pan, música) con evidencia `estático`.
- [ ] CA4: cada pedido nuevo de `coordination/rea-re.md` queda respondido con VA o marcado explícitamente como
      pendiente/bloqueado (no se deja sin respuesta).

## Diseño
El proveedor REA-Ghidra importa el exe a un proyecto efímero con auto-análisis default; no hay nombres previos
(el proyecto de `shared/ghidra-proyectos/HotWheels` es de Ghidra 11 y no se reusa). Por eso las citas van por VA,
no por nombre. Lecturas ya hechas: `docs/re-engine.md` (mapa de nombres/VA), `FINDINGS.md` #18 (carrera D3D6 medido
en Wine), `tools/sfp_dump.py` + volcado propio de los 9 SFP (páginas de textura, chunks), y las tablas de punteros
de `DATA\*` en `.rdata`.

**Rol técnico obligatorio (punto 11 del brief común):** los entregables incluyen el razonamiento a nivel SO/hardware
pedido por el usuario, derivado del mecanismo del exe, en `docs/re-tracks.md` §7 (ownership CPU↔GPU de cada buffer,
orden de memoria ARMv7 vs. TSO del guest, y bordes) y `docs/re-audio.md` §4 (ownership del PCM, concurrencia
guest↔mezclador, bordes del tope de voces/volumen/frecuencia). No es código: es el contrato que el shim debe cumplir.

## No-objetivos
No reemplazar `docs/re-engine.md` ni el proyecto Ghidra 11; no ejecutar el exe (eso es del oráculo Wine/qemu/
Vita3K de otros agentes); no tocar código de otros dueños; no commitear bytes ni assets del juego.

## Riesgos / preguntas abiertas
El auto-análisis default puede no crear funciones alcanzadas sólo por puntero (p. ej. estados de la máquina
`0x4111b0`): se resuelven con `resolve_containing_procedure`/`read_function_instructions`, no con `decompile`.
Que la interpretación de un registro de formato (bits por canal) sea exacta se confirma con la traza del propio
juego ("Using %i bits textures (%i%i%i%i)", VA `0x462b84`) en Wine, no con REA.

## Evidencia
`estático` (Ghidra 12.1.4 vía REA y volcado de datos con Python). Los contrastes con lo medido en Wine se citan
como `[wine]` y provienen de `FINDINGS.md`/`docs/wine-reference.md`, no de esta feature.
