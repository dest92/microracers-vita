#!/usr/bin/env bash
# tools/build_diag_vpk.sh [--dry-run] V0|V1|V2... — VPKs de diagnóstico de BUG-047 (SPEC-052 CA5).
# Cada variante compila un clon aislado de vitaGL (mismo SHA y flags que build_vitagl.sh; el SDK instalado no se toca)
# y enlaza el VPK contra ese libvitaGL.a vía HW_VITAGL_LIBDIR de build_vita.sh:
#   V0 = flags de producción (control)  V1 = + SAFE_DRAW/SAFE_UNIFORMS  V2 = + NO_CIRCULAR_POOL
# Si existe patches/vitagl-c707ff00-diag.patch (contadores de cc-gfx) se aplica a todas. VGL_DIAG_VARIANT llega a make.
# Salida: out/diag-vpk/hotwheels-diag-<V>.vpk + MANIFEST.txt (reescrito en cada invocación: SHA de VPK/lib/parche,
# flags, commit) + src-<sha12>.diff (WIP + no versionados del árbol compilado: el build toma el working tree, no HEAD;
# nombrado por su SHA para que un build posterior no pise el diff de otro). Construir las variantes a comparar en UNA
# invocación: así comparten árbol. --dry-run imprime el plan sin compilar.
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
DRY=0; [ "${1:-}" = --dry-run ] && { DRY=1; shift; }
[ $# -ge 1 ] || { sed -n 2,8p "$0"; exit 2; }
VGL_SHA="$(sed -n 's/^VITAGL_SHA="\([0-9a-f]*\)"$/\1/p' "$HW/tools/build_vitagl.sh")"
BASE="$(grep -o 'make install -j4 HAVE_[A-Z0-9_= ]*' "$HW/tools/build_vitagl.sh" | sed 's/make install -j4 //; s/ *$//')"
PATCH="$HW/patches/vitagl-c707ff00-diag.patch"
OUTD="$HW/out/diag-vpk"
extra_for() {
  case "$1" in V0) echo "" ;; V1) echo "SAFE_DRAW=1 SAFE_UNIFORMS=1" ;; V2) echo "NO_CIRCULAR_POOL=1" ;;
    *) echo "variante desconocida: $1 (V0|V1|V2)" >&2; return 2 ;; esac
}
sha() { sha256sum "$1" | cut -d' ' -f1; }
for v in "$@"; do extra_for "$v" >/dev/null || exit 2; done
export VITASDK="${VITASDK:-$HW/../shared/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
tree_diff() {  # WIP + no versionados que compila build_vita.sh (el working tree, no HEAD)
  git -C "$HW" diff HEAD -- src tools ../winx86-dx
  git -C "$HW" ls-files --others --exclude-standard -- src tools ../winx86-dx | sed 's/^/untracked: /'
}
if [ "$DRY" = 0 ]; then
  mkdir -p "$OUTD"
  REPO="$(git -C "$HW" rev-parse --short HEAD)"
  tree_diff > "$OUTD/src.diff.tmp"
  SRC_SHA="$(sha "$OUTD/src.diff.tmp")"
  mv "$OUTD/src.diff.tmp" "$OUTD/src-${SRC_SHA:0:12}.diff"
  : > "$OUTD/MANIFEST.txt"
fi
for v in "$@"; do
  extra="$(extra_for "$v")"
  flags="$BASE${extra:+ $extra} VGL_DIAG_VARIANT=$v"
  src="$HW/out/diag-vgl-$v"; vpk="$OUTD/hotwheels-diag-$v.vpk"
  echo "variant=$v vgl_sha=$VGL_SHA make_flags=\"$flags\" patch=$([ -f "$PATCH" ] && echo "$PATCH" || echo -) vpk=$vpk"
  [ "$DRY" = 1 ] && continue
  rm -rf "$src"
  git clone -q --no-checkout "$HW/third_party/vitaGL" "$src"
  git -C "$src" checkout -q "$VGL_SHA"
  [ -f "$PATCH" ] && git -C "$src" apply "$PATCH"
  # shellcheck disable=SC2086  # flags = palabras KEY=VAL para make
  make -C "$src" -j4 $flags > "$src.build.log" 2>&1 || { tail -20 "$src.build.log" >&2; exit 1; }
  HW_VITA_BUILD_DIR="$HW/out/diag-build-$v" HW_VITAGL_LIBDIR="$src" bash "$HW/tools/build_vita.sh" > "$src.vpk.log" 2>&1 \
    || { tail -20 "$src.vpk.log" >&2; exit 1; }
  cp "$HW/out/diag-build-$v/hotwheels-vita.vpk" "$vpk"
  # Otro agente puede commitear o editar durante el build: si HEAD o el árbol cambiaron, el binario no
  # corresponde exactamente a repo+src_diff y la línea lo dice.
  changed=0
  [ "$(git -C "$HW" rev-parse --short HEAD)" = "$REPO" ] && [ "$(tree_diff | sha256sum | cut -d' ' -f1)" = "$SRC_SHA" ] || changed=1
  { echo "variant=$v vpk_sha256=$(sha "$vpk") vpk_bytes=$(stat -c %s "$vpk") libvitagl_sha256=$(sha "$src/libvitaGL.a")" \
         "vgl_sha=$VGL_SHA patch_sha256=$([ -f "$PATCH" ] && sha "$PATCH" || echo -) make_flags=\"$flags\"" \
         "repo=$REPO src_diff=src-${SRC_SHA:0:12}.diff tree_changed=$changed $(date -Iseconds)"
  } >> "$OUTD/MANIFEST.txt"
  tail -1 "$OUTD/MANIFEST.txt"
done
