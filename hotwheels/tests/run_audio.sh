#!/usr/bin/env bash
# tests/run_audio.sh — HW-018: corre los tests host del backend de audio (spec 009).
# No requiere vitasdk ni winx86. El WAV de salida (build/test-audio-full.wav) es la
# evidencia offline del mezclador.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address -fno-omit-frame-pointer -pthread -Isrc \
  tests/test_audio.cpp \
  src/audio/mixer.cpp \
  src/audio/wav.cpp \
  src/audio/music.cpp \
  src/audio/dsound_bus.cpp \
  src/audio/engine.cpp \
  -o build/test_audio
build/test_audio
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=thread -fno-omit-frame-pointer -fno-pie -no-pie -pthread -Isrc \
  tests/test_audio.cpp \
  src/audio/mixer.cpp \
  src/audio/wav.cpp \
  src/audio/music.cpp \
  src/audio/dsound_bus.cpp \
  src/audio/engine.cpp \
  -o build/test_audio_tsan
setarch "$(uname -m)" -R build/test_audio_tsan
c++ -std=gnu++17 -Wall -Wextra -Werror -Isrc tests/test_audio_underrun.cpp src/audio/mixer.cpp src/audio/dsound_bus.cpp -o build/test_audio_underrun -lpthread
build/test_audio_underrun
c++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/test_audio_short_burst.cpp src/audio/mixer.cpp src/audio/dsound_bus.cpp -o build/test_audio_short_burst -pthread
build/test_audio_short_burst
c++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/test_audio_metrics_gate.cpp -o build/test_audio_metrics_gate
build/test_audio_metrics_gate
c++ -std=gnu++17 -Wall -Wextra -Werror -Isrc tests/test_audio_selfpaced.cpp src/audio/mixer.cpp src/audio/dsound_bus.cpp -o build/test_audio_selfpaced -lpthread
build/test_audio_selfpaced
c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I../winx86-dx/include tests/test_winx86_dx_audio.cpp \
  ../winx86-dx/src/audio/mixer.cpp ../winx86-dx/src/audio/dsound_bus.cpp \
  -o build/test_winx86_dx_audio
build/test_winx86_dx_audio
