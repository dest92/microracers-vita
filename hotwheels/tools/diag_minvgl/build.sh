#!/usr/bin/env bash
# Construye eboot.bin de diag_minvgl (se despliega a mano como eboot de HWMR00002).
set -euo pipefail
D="$(cd "$(dirname "$0")" && pwd)"; HW="$D/../.."
export VITASDK="${VITASDK:-$HW/../shared/vitasdk}"; export PATH="$VITASDK/bin:$PATH"
OUT="$HW/build-diag-minvgl${OUT_SUFFIX:-}"; mkdir -p "$OUT"
arm-vita-eabi-gcc -O2 ${CFLAGS_EXTRA:-} -c "$D/main.c" -o "$OUT/main.o"
arm-vita-eabi-g++ -Wl,-q "$OUT/main.o" -o "$OUT/minvgl.elf" -lvitaGL -lvitashark -lSceShaccCgExt -lmathneon -ltaihen_stub \
  -lSceShaccCg_stub -lSceAppMgr_stub -lSceKernelDmacMgr_stub -lSceDisplay_stub -lSceGxm_stub -lSceSysmodule_stub -lSceLibKernel_stub -lSceKernelThreadMgr_stub -lSceSysmem_stub -lSceProcessmgr_stub -lSceRtc_stub -lSceAppUtil_stub -lSceVshBridge_stub \
  -lSceNet_stub -lSceCommonDialog_stub -lSceIofilemgr_stub -lSceCtrl_stub -lSceTouch_stub -lSceAudio_stub -lScePower_stub -lm -lc
vita-elf-create "$OUT/minvgl.elf" "$OUT/minvgl.velf" >/dev/null
vita-make-fself -s "$OUT/minvgl.velf" "$OUT/eboot.bin" >/dev/null
ls -la "$OUT/eboot.bin"
