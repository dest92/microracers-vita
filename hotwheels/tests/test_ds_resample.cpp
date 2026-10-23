// test_ds_resample.cpp — mezcla por voz de ds_emul (winx86 ds_resample.h):
// identidad a la frecuencia de salida, interpolación lineal al re-muestrear y
// continuidad de fase con SetFrequency cada 25 ms (motor de Hot Wheels,
// FUN_0044fb30). Sin Cpu ni hilos.
#include "runtime/ds_resample.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { std::printf("FAIL %d %s\n", __LINE__, m); ++g_fail; } } while (0)
using d2rt::dsound::ResampleVoice;
using d2rt::dsound::mix_voice;
constexpr uint32_t kOut = 22050, kUnity = 65536;

static ResampleVoice mono(const std::vector<int16_t>& s, uint32_t rate, bool loop) {
    return ResampleVoice{reinterpret_cast<const uint8_t*>(s.data()), uint32_t(s.size() * 2), 0, 2, rate, 0, 1,
                         kUnity, kUnity, loop};
}

int main() {
    // 1) rate == salida: copia exacta muestra a muestra.
    std::vector<int16_t> ramp(512);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = int16_t(i * 37 - 9000);
    { ResampleVoice v = mono(ramp, kOut, false); std::vector<int32_t> out(2 * 256, 0);
      mix_voice(v, kOut, 256, out.data());
      bool same = true; for (int i = 0; i < 256; ++i) same &= out[2 * i] == ramp[i] && out[2 * i + 1] == ramp[i];
      CHECK(same, "identidad a 22050 Hz"); }
    // 2) a la mitad de frecuencia, los puntos intermedios interpolan (no repiten).
    { ResampleVoice v = mono(ramp, kOut / 2, false); std::vector<int32_t> out(2 * 200, 0);
      mix_voice(v, kOut, 200, out.data());
      bool lin = true;
      for (int i = 0; i < 100; ++i) {
          lin &= out[4 * i] == ramp[i];
          lin &= std::abs(out[4 * i + 2] - (ramp[i] + ramp[i + 1]) / 2) <= 1;
      }
      CHECK(lin, "interpolación lineal al re-muestrear (no vecino más cercano)"); }
    // 3) motor: seno 220 Hz grabado a 11025 Hz, SetFrequency cada 25 ms entre
    //    8000 y 30000 Hz, mezclado por granos. Sin clicks: ningún salto entre
    //    muestras consecutivas supera el máximo que permite la señal interpolada.
    std::vector<int16_t> sine(11025);
    for (size_t i = 0; i < sine.size(); ++i) sine[i] = int16_t(12000 * std::sin(2 * M_PI * 220.0 * double(i) / 11025.0));
    { ResampleVoice v = mono(sine, 11025, true);
      const int grain = int(kOut / 40);   // 25 ms
      std::vector<int32_t> all;
      uint32_t f = 8000;
      for (int g = 0; g < 80; ++g) {
          std::vector<int32_t> out(2 * grain, 0);
          v.rate = f;                                   // SetFrequency entre granos
          mix_voice(v, kOut, grain, out.data());
          for (int i = 0; i < grain; ++i) all.push_back(out[2 * i]);
          f = (f * 7 + 3001) % 22001 + 8000;
      }
      // Máximo delta de la fuente por muestra de origen y paso máximo 30000/22050.
      int32_t src_max = 0;
      for (size_t i = 1; i < sine.size(); ++i) src_max = std::max(src_max, std::abs(sine[i] - sine[i - 1]));
      int32_t out_max = 0;
      for (size_t i = 1; i < all.size(); ++i) out_max = std::max(out_max, std::abs(all[i] - all[i - 1]));
      const int32_t bound = int32_t(src_max * (30000.0 / kOut)) + 2;
      if (out_max > bound) std::printf("  salto máximo %d > cota %d\n", out_max, bound);
      CHECK(out_max <= bound, "SetFrequency cada 25 ms sin clicks (fase continua)"); }
    // 4) one-shot: al final se silencia y devuelve false.
    { std::vector<int16_t> s(8, 1000); ResampleVoice v = mono(s, kOut, false); std::vector<int32_t> out(2 * 16, 0);
      CHECK(!mix_voice(v, kOut, 16, out.data()) && out[2 * 15] == 0, "one-shot termina en silencio"); }
    if (g_fail) { std::printf("DS-RESAMPLE: %d FALLAS\n", g_fail); return 1; }
    std::printf("DS-RESAMPLE: PASS\n");
}
