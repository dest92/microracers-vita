# BUG-050 — HEAD rojo en checkout limpio: `test_track_oracle.py` lee la fixture NO versionada `tools/race-d3d-settings.dat`

Nivel: **medido** (oc-qa, 2026-10-21 ~21:17, HEAD `9580f82`, worktree aislado sin árbol compartido). Dueño: **opus-gfx** (HW-067, oráculo por pista). Severidad: mayor (`run_all` rojo en cualquier checkout limpio y en el worker remoto). Estado: abierto.

## Síntoma (exacto, logs)
`hotwheels/out/qa-clean-m8/test_track_oracle.py.log`:
```
FileNotFoundError: [Errno 2] No such file or directory:
  '.../hotwheels/tools/race-d3d-settings.dat'
```
En el árbol compartido el test **pasa** porque el archivo existe como untracked:
```
$ git ls-files --error-unmatch hotwheels/tools/race-d3d-settings.dat
error: ... Did you forget to 'git add'?
$ git check-ignore -v hotwheels/tools/race-d3d-settings.dat   # sin salida: no está ignorado
$ wc -c hotwheels/tools/race-d3d-settings.dat
36 hotwheels/tools/race-d3d-settings.dat   (17 líneas de dígitos)
```

## Reproducción mínima
```bash
git worktree add --detach /tmp/q 9580f82
cd /tmp/q/hotwheels && python3 tests/test_track_oracle.py   # FileNotFoundError
```

## Esperado vs observado
- Esperado: todo insumo del test está versionado (o lo genera el propio test); un checkout limpio y el worker remoto (`tools/remote_run.sh`, que sincroniza sólo lo versionado + rutas incluidas) reproducen el PASS del árbol compartido.
- Observado: el PASS depende de un archivo untracked de 36 B que nadie recrea.

## Causa raíz (verificada)
`tests/test_track_oracle.py:75` (`base = (HW / "tools" / "race-d3d-settings.dat").read_text().split()`) se agregó en `7d87219` (oráculo por pista, settings por pista) sin commitear la fixture. Los otros `.dat` de la misma familia (`race-d3d-track1..3.dat`, `race-sw-settings.dat`) tampoco están versionados.

## Fix + test de regresión
- Commitear `hotwheels/tools/race-d3d-settings.dat` (+ los `race-d3d-track*.dat` / `race-sw-settings.dat` que usan los runners del oráculo): son 36 B de parámetros, sin contenido del juego.
- Regresión: `run_all` verde en worktree limpio (sin `out/` ni archivos untracked).
- Hecho (opus-gfx, ad4692e): versionados `race-d3d-settings.dat` y `race-sw-settings.dat`. Los `race-d3d-track1..3.dat` ya
  estaban versionados desde 7d87219 (`git ls-files tools/*.dat`). Verificado: `git archive HEAD` + `test_track_oracle.py` → OK.
  Falta la corrida de `run_all` en worktree limpio de oc-qa para cerrar.

## Verificación y cierre (oc-qa, 2026-10-21; HEAD `8c7fc17`)
- Fix `ad4692e` versiona `tools/race-d3d-settings.dat` + `tools/race-sw-settings.dat` (los `race-d3d-track*.dat` ya estaban desde `7d87219`).
- Worktree aislado de HEAD (sin `out/` del árbol compartido, `shared/*` symlinkado): `run_all` → **PASS=57 FAIL=4 SKIP=3**; `test_track_oracle.py` **PASS**.
- Los 4 FAIL restantes son de otros bugs abiertos (BUG-048 ×2, BUG-049, BUG-051), no de esta suite.
