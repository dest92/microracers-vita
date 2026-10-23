#!/usr/bin/env bash
# tools/build_vita.sh — VPK del esqueleto hotwheels-vita (spec 001, A5).
# Enlaza libwinx86_vita.a de winx86 (referenciado, no copiado). Siempre invoca
# el build.sh de winx86 (TARGET=vita, incremental) para no enlazar un .a viejo.
# Requiere hotwheels/patches/winx86-*.patch aplicados (ver docs/winx86-patches.md).
# HW_VITAGL_LIBDIR=<dir>: enlaza el libvitaGL.a de ese dir antes que el del SDK (variantes de build_diag_vpk.sh).
set -euo pipefail
HW="$(cd "$(dirname "$0")/.." && pwd)"
WX86="${WINX86_DIR:-$HW/../d2vita/D2Vita/third_party/winx86}"
export VITASDK="${VITASDK:-$HW/../shared/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
OUT="${HW_VITA_BUILD_DIR:-$HW/build-vita}"
mkdir -p "$OUT"
LIB="$WX86/build-vita/libwinx86_vita.a"
TARGET=vita bash "$WX86/build.sh" >/dev/null

CXXFLAGS="-std=gnu++17 -O2 -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard \
  -D__vita__ -DD2RT_CPU_BOX86 -I$HW/src -I$WX86/src -I$WX86/src/dynarec86 \
  -I$WX86/src/dynarec86/shim/vita -I$WX86/src/dynarec86/shim \
  -Wall -Wno-unused-parameter -MMD -MP"
SRCS=("$HW/src/runtime_vita.cpp" "$HW/src/runtime/guest_eip_profile.cpp" "$HW/src/boot/pe_file.cpp" "$HW/src/boot/boot.cpp" "$HW/src/runtime/hw_layout.cpp" "$HW/src/runtime/hw_intrin.cpp" "$HW/src/hle/span_ft3.c" "$HW/src/hle/emu_glue.c"
  "$HW/src/d3d/core.cpp" "$HW/src/d3d/bridge.cpp" "$HW/src/d3d/frame_queue.cpp" "$HW/src/d3d/vitagl_backend.cpp"
  "$HW/src/fps/interp60.cpp"
  "$HW/src/host/integrate.cpp" "$HW"/src/shims/*.cpp "$HW"/src/ddraw/*.cpp
  "$HW/src/dinput/dinput.cpp" "$HW/src/dinput/vita_input.cpp"
  "$HW"/src/io/*.cpp "$HW"/src/integration/*.cpp "$HW"/src/audio/*.cpp
  "$HW/src/winmm/mci.cpp" "$HW/src/winmm/mmio.cpp" "$HW/src/present/vita_present.cpp" "$HW/src/present/present_math.cpp"
  "$HW/src/diag/fault_report_vita.cpp")
OBJS=()
for s in "${SRCS[@]}"; do
  o="$OUT/$(echo "${s#$HW/src/}" | tr / _ | sed 's/\.cpp$/.o/; s/\.c$/.o/')"
  rebuild=0
  if [ ! -f "$o" ] || [ ! -f "${o%.o}.d" ] || [ "$0" -nt "$o" ]; then
    rebuild=1
  else
    # .d del compilador incluye todos los headers transitivos.
    while IFS= read -r dep; do
      if [ ! -e "$dep" ] || [ "$dep" -nt "$o" ]; then rebuild=1; break; fi
    done < <(sed -e 's/^[^:]*://' -e 's/\\$//' "${o%.o}.d" | tr ' ' '\n' | sed '/^$/d')
  fi
  if [ "$rebuild" = 1 ]; then
    case "$s" in
      *.c) # HLE (HW-052): C puro; emu_glue.c ve los internos de box86.
        arm-vita-eabi-gcc -std=gnu11 -O2 -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -D__vita__ \
          -I"$HW/src" -I"$WX86/src" -I"$WX86/src/dynarec86" -I"$WX86/src/dynarec86/shim/vita" -I"$WX86/src/dynarec86/shim" \
          -I"$WX86/third_party/box86-dynarec/include" -I"$WX86/third_party/box86-dynarec" \
          -Wall -MMD -MP -c "$s" -o "$o" ;;
      *) arm-vita-eabi-g++ $CXXFLAGS -c "$s" -o "$o" ;;
    esac
  fi
  OBJS+=("$o")
done
D2="$WX86/../.."
link_elf() {  # $@ = objetos extra (relleno de segmento)
arm-vita-eabi-g++ -Wl,-q -o "$OUT/hotwheels-vita.elf" "${OBJS[@]}" "$@" \
  ${HW_VITAGL_LIBDIR:+-L"$HW_VITAGL_LIBDIR"} -Wl,--start-group "$LIB" -lvitaGL -lmathneon -lvitashark -lSceShaccCgExt -Wl,--end-group \
  -lSceShaccCg_stub -lSceAppMgr_stub -lSceKernelDmacMgr_stub \
  -lSceDisplay_stub -lSceCtrl_stub -lSceTouch_stub -lSceSysmem_stub -lSceLibKernel_stub \
  -lSceIofilemgr_stub -lSceProcessmgr_stub -lSceKernelThreadMgr_stub -lSceRtc_stub \
  -lSceAppUtil_stub -lSceSysmodule_stub -lScePower_stub -lSceAudio_stub \
  -lSceNet_stub -lSceNetCtl_stub -lSceGxm_stub -lSceCommonDialog_stub -ltaihen_stub_weak \
  -L"$D2/third_party/kubridge-stub" -lkubridge_stub_weak \
  -L"$D2/third_party/vpmu-stub" -lvpmu_stub_weak -lSceVshBridge_stub \
  -Wl,--wrap=dyn86_fault_handle -Wl,-u,pthread_cancel -Wl,-u,pthread_once -lpthread -lm -lz
}
# vita-elf-create necesita ~4 KiB libres al final del segmento 0 (texto) para
# las tablas SCE; si el texto termina a <4136 B del inicio de datos (alineado a
# 64 KiB) segfaultea ("segment 1 overlaps"). Depende del tamaño del código:
# medido 2026-10-03 con hueco 1404 B. Si el hueco es <8 KiB, se reenlaza con un
# relleno en .rodata que empuja los datos a la siguiente página de 64 KiB.
seg_gap() { python3 - "$OUT/hotwheels-vita.elf" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
phoff, = struct.unpack_from('<I', d, 0x1c); n, = struct.unpack_from('<H', d, 0x2c)
loads = [struct.unpack_from('<8I', d, phoff + i * 32) for i in range(n)]
loads = [p for p in loads if p[0] == 1]
print(loads[1][2] - (loads[0][2] + loads[0][5]))
PY
}
link_elf
GAP=$(seg_gap)
if [ "$GAP" -lt 8192 ]; then
  printf '.section .rodata.hw_segpad,"a"\n.space %d\n' $((GAP + 256)) > "$OUT/segpad.s"
  arm-vita-eabi-as "$OUT/segpad.s" -o "$OUT/segpad.o"
  link_elf "$OUT/segpad.o"
  echo "segpad: hueco texto→datos ${GAP} B < 8 KiB; relleno $((GAP + 256)) B → $(seg_gap) B" >&2
fi
vita-elf-create "$OUT/hotwheels-vita.elf" "$OUT/hotwheels-vita.velf" >/dev/null
# -c: segmentos comprimidos (zlib). 24.4 MB → 1.9 MB medido 2026-10-03: el FSELF
# sin comprimir arrastra ~19 MB de debug/relocs; cada deploy los subía y releía
# por FTP. La Vita descomprime al cargar. Símbolos: usar el .elf local.
vita-make-fself -s -c "$OUT/hotwheels-vita.velf" "$OUT/eboot.bin" >/dev/null
# param.sfo + VPK (con LiveArea/icono) delegados a package.sh (HW-017).
# Fuente única de verdad de TITLEID/versión: tools/app.conf.
bash "$HW/tools/package.sh" "$OUT"
echo "OK $OUT/hotwheels-vita.vpk ($(stat -c %s "$OUT/hotwheels-vita.vpk") bytes)"
