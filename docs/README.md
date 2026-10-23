# Sistema de documentación (SDD)
Por proyecto (`hotwheels/`, `d2vita/`):
| archivo | qué es | quién escribe |
|---|---|---|
| `MILESTONES.md` | hitos M0..Mn, cada uno con criterios de salida y features | orquestador |
| `feature_list.json` | features (HW-NNN) con deps, status, owner, milestone, spec | orquestador asigna, agente actualiza status |
| `specs/NNN-nombre.md` | spec por feature (plantilla `docs/templates/spec.md`) ANTES de codear | agente dueño |
| `FINDINGS.md` | hechos aprendidos, numerados, con nivel de evidencia | cualquiera |
| `bugs/BUG-NNN.md` + `BUGS.md` (índice) | defectos (plantilla `docs/templates/bug.md`) | cualquiera |
| `PROGRESS.md` | bitácora cronológica corta | orquestador + dueños |
| `coordination/` (raíz) | estado por agente, reviews | agentes |
Global: `docs/vita-hardware.md` (plataforma), `ORCHESTRATION.md`, `AGENTS.md`.

Flujo de una feature: spec (status `spec`) → review de spec (luna) → test rojo → impl → test verde → code review (reviewer) → `done` + FINDINGS/PROGRESS.
Estados: `todo` → `spec` → `in_progress` → `review` → `done` (o `blocked` con motivo).
