#!/usr/bin/env bash
# tools/build_host.sh — hotwheels-host (HW-007, tier 1 qemu-arm): ARMv7 Linux
# estático que enlaza libwinx86.a (target qemu-arm de winx86) + shims del port.
# Corre DENTRO de la imagen hw-host-arm (tools/host/Dockerfile); run_host.sh lo
# invoca. Salida: hotwheels/build-host/hotwheels-host.
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
WX86="${WINX86_DIR:-$HW/../d2vita/D2Vita/third_party/winx86}"
OUT="${HW_HOST_BUILD_DIR:-$HW/build-host}"
mkdir -p "$OUT/obj"
# winx86 (incremental; build-arm/libwinx86.a).
bash "$WX86/build.sh" >/dev/null
CXX=arm-linux-gnueabihf-g++
CXXFLAGS="-std=gnu++17 -O2 -g -marm -march=armv7-a+simd -mfpu=neon -mfloat-abi=hard \
  -DD2RT_CPU_BOX86 -I$HW/src -I$HW/../winx86-dx/include -I$WX86/src -I$WX86/src/dynarec86 -I$WX86/src/dynarec86/shim \
  -Wall -Wno-unused-parameter -MMD -MP"
SRCS=(
  "$HW/src/host/host_main.cpp" "$HW/src/host/integrate.cpp"
  "$HW/src/d3d/core.cpp" "$HW/src/d3d/bridge.cpp" "$HW/src/d3d/frame_queue.cpp" "$HW/src/d3d/refraster.cpp"
  "$HW/src/boot/pe_file.cpp" "$HW/src/boot/boot.cpp" "$HW/src/runtime/hw_layout.cpp" "$HW/src/runtime/hw_intrin.cpp" "$HW/src/runtime/guest_eip_profile.cpp" "$HW/src/runtime/guest_dt_profile.cpp"
  "$HW/src/hle/span_ft3.c" "$HW/src/hle/emu_glue.c" "$HW/src/diag/pc_profile.c"
  "$HW"/src/io/read_stream.cpp $(ls "$HW"/src/shims/*.cpp "$HW"/src/ddraw/*.cpp "$HW"/src/dinput/dinput.cpp)
  "$HW/src/integration/scripted_input.cpp" "$HW/src/integration/dinput_bridge.cpp" "$HW/src/integration/dsound_bridge.cpp" "$HW/src/integration/mci_bridge.cpp" "$HW/src/integration/mmio_bridge.cpp"
  $(ls "$HW"/src/audio/*.cpp) "$HW/src/winmm/mci.cpp" "$HW/src/winmm/mmio.cpp"
)
OBJS=()
PIDS=()
for s in "${SRCS[@]}"; do
  o="$OUT/obj/$(echo "${s#$HW/src/}" | tr / _ | sed 's/\.cpp$/.o/; s/\.c$/.o/')"
  if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || { [ -f "${o%.o}.d" ] && \
       [ -n "$(find $(sed -e 's/^[^:]*://' -e 's/\\$//' "${o%.o}.d" | tr ' ' '\n' | grep -v '^$' | sort -u) -newer "$o" 2>/dev/null | head -1)" ]; }; then
    case "$s" in
      *.c) # HLE (HW-052): C puro; emu_glue.c ve los internos de box86.
        arm-linux-gnueabihf-gcc -std=gnu11 -O2 -g -marm -march=armv7-a+simd -mfpu=neon -mfloat-abi=hard \
          -I$HW/src -I$WX86/src -I$WX86/src/dynarec86 -I$WX86/src/dynarec86/shim \
          -I$WX86/third_party/box86-dynarec/include -I$WX86/third_party/box86-dynarec \
          -Wall -MMD -MP -c "$s" -o "$o" & PIDS+=("$!") ;;
      *) $CXX $CXXFLAGS -c "$s" -o "$o" & PIDS+=("$!") ;;
    esac
  fi
  OBJS+=("$o")
done
BUILD_FAILED=0
for pid in "${PIDS[@]}"; do
  if ! wait "$pid"; then BUILD_FAILED=1; fi
done
[ "$BUILD_FAILED" -eq 0 ] || { echo "error: falló la compilación de uno o más objetos host" >&2; exit 1; }
$CXX -static -o "$OUT/hotwheels-host" "${OBJS[@]}" \
  -Wl,--start-group "$WX86/build-arm/libwinx86.a" -Wl,--end-group -lpthread -lm \
  -Wl,-u,pthread_cancel -Wl,-u,pthread_once
echo "OK $OUT/hotwheels-host"
