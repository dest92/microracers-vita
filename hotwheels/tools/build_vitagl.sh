#!/usr/bin/env bash
# build_vitagl.sh — builds vitaGL + its dependencies into $VITASDK (HW-011).
#
# vitaGL (LGPL-3.0) is not in vdpm, so it is built from source. It needs:
#   math-neon   (MIT)        -> libmathneon.a / math_neon.h
#   SceShaccCgExt (GPL-3.0)  -> libSceShaccCgExt.a / shacccg_ext.h
#   vitaShaRK   (MIT)        -> libvitashark.a / vitashark.h
# then vitaGL itself -> libvitaGL.a / vitaGL.h
#
# Repos are cloned under hotwheels/third_party/ (gitignored) at the commits
# exercised by HW-011. On success, the sample in samples/vitagl-ffp builds.
#
# Usage:  export VITASDK=/path/to/vitasdk; ./hotwheels/tools/build_vitagl.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"      # hotwheels/
TP="$ROOT/third_party"
: "${VITASDK:?set VITASDK to your vitasdk prefix}"
export PATH="$VITASDK/bin:$PATH"

# Pinned commits verified by HW-011 (2026-10-02).
VITAGL_REPO="https://github.com/Rinnegatamante/vitaGL"
VITAGL_SHA="c707ff00d9a372f0ba7056b4a4b723f5a00ba880"
SHARK_REPO="https://github.com/Rinnegatamante/vitaShaRK"
SHARK_SHA="df24065e65098b2d1ac533760109ad4367573f28"
MATHNEON_REPO="https://github.com/Rinnegatamante/math-neon"
MATHNEON_SHA="0faab814782c071ff4015527f1ca955ab1ccc470"
SCECG_REPO="https://github.com/bythos14/SceShaccCgExt"
SCECG_SHA="fb0e9d338525b067f3679ab33571323336493cca"

clone() { # url dir sha
	local url="$1" dir="$2" sha="$3"
	if [ ! -d "$dir/.git" ]; then
		echo "== clone $url"
		rm -rf "$dir"
		git clone --quiet "$url" "$dir"
	fi
	git -C "$dir" fetch --quiet origin "$sha" 2>/dev/null || true
	git -C "$dir" checkout --quiet "$sha" 2>/dev/null || \
		echo "   (warning: could not checkout $sha, using default branch)"
}

mkdir -p "$TP"

# 1) math-neon — GCC>=15 defaults to C23 where '()' means '(void)', which
#    breaks old-style hook casts; pin gnu17.
clone "$MATHNEON_REPO" "$TP/math-neon" "$MATHNEON_SHA"
( cd "$TP/math-neon" && make clean >/dev/null 2>&1 || true
  make install -j4 CFLAGS="-g -Wl,-q -O2 -ffast-math -mtune=cortex-a9 -mfpu=neon -ftree-vectorize -std=gnu17" )

# 2) SceShaccCgExt — same gnu17 fix; installs lib + header.
clone "$SCECG_REPO" "$TP/SceShaccCgExt" "$SCECG_SHA"
( cd "$TP/SceShaccCgExt" && rm -rf build && mkdir build && cd build
  cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-std=gnu17" .. >/dev/null
  make -j4 && make install )

# 3) vitaShaRK — needs shacccg_ext.h from step 2.
clone "$SHARK_REPO" "$TP/vitaShaRK" "$SHARK_SHA"
( cd "$TP/vitaShaRK" && make clean >/dev/null 2>&1 || true
  make install -j4 CFLAGS="-g -Wl,-q -O2 -ffast-math -mtune=cortex-a9 -mfpu=neon -ftree-vectorize -std=gnu17" )

# 4) vitaGL — FFP + Vita3K-friendly (HW ETC1 disabled). HAVE_FFP_SHADER_SUPPORT
#    enables GLSL 1.20 built-in uniform bindings used by fixed-function shaders.
clone "$VITAGL_REPO" "$TP/vitaGL" "$VITAGL_SHA"
( cd "$TP/vitaGL" && make clean >/dev/null 2>&1 || true
  make install -j4 HAVE_VITA3K_SUPPORT=1 HAVE_FFP_SHADER_SUPPORT=1 )

echo "== installed:"
ls -1 "$VITASDK/arm-vita-eabi/lib/libvitaGL.a" \
      "$VITASDK/arm-vita-eabi/lib/libvitashark.a" \
      "$VITASDK/arm-vita-eabi/lib/libmathneon.a" \
      "$VITASDK/arm-vita-eabi/lib/libSceShaccCgExt.a"
echo "OK. Now: make -C $ROOT/samples/vitagl-ffp"
