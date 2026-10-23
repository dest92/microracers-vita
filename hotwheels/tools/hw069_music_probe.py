#!/usr/bin/env python3
"""hw069_music_probe.py — SPEC-048 (HW-069): ¿la música MCI llega a la mezcla final?

Los WAV de `hw069-listen` (linear/cubic/sinc*) son el render offline de la captura
DirectSound: **no** tienen música (el sweep sólo rehace las fuentes DS). Este probe
mide sobre la mezcla REAL del Engine host (HW_WAV=1, 48 kHz, efectos + música) si
la pista MCI está presente.

- Extrae la ventana pedida a un WAV de 48 kHz para el oído.
- Correlaciona la pista (remapeada 44.1k→48k lineal, como el Mixer) contra la
  ventana; un pico de correlación consistente en ambos canales prueba presencia.
- Estima la ganancia por proyección (m ≈ g·s + ruido) y el RMS de la ventana.

Es una medición de PRESENCIA/nivel, no de fidelidad: la pista está looped en un
offset desconocido y arriba hay efectos no correlacionados.
"""
import argparse
import wave

import numpy as np


def read_wav(path, max_seconds=None):
    with wave.open(path, "rb") as w:
        rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        if max_seconds:
            n = min(n, int(max_seconds * rate))
        data = np.frombuffer(w.readframes(n), dtype="<i2").reshape(-1, ch).astype(np.float64)
    return data, rate, ch


def write_wav(path, data, rate):
    with wave.open(path, "wb") as w:
        w.setnchannels(data.shape[1])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(np.clip(data, -32768, 32767).astype("<i2").tobytes())


def resample_linear(x, src_rate, dst_rate):
    n_out = int(len(x) * dst_rate / src_rate)
    t = np.arange(n_out) * (src_rate / dst_rate)
    i0 = np.clip(np.floor(t).astype(int), 0, len(x) - 2)
    fr = (t - i0)[:, None]
    return x[i0] + (x[i0 + 1] - x[i0]) * fr


def best_lag(signal, template):
    n = 1 << int(np.ceil(np.log2(len(signal) + len(template))))
    cc = np.fft.irfft(np.fft.rfft(signal, n) * np.conj(np.fft.rfft(template, n)), n)
    k = int(np.argmax(np.abs(cc)))
    peak = cc[k] / len(signal)
    coef = peak / (np.sqrt(np.mean(signal ** 2)) * np.sqrt(np.mean(template ** 2)) + 1e-12)
    gain_db = 20 * np.log10(abs(peak) / (np.mean(template ** 2) + 1e-12) + 1e-12)
    return k, coef, gain_db


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mix", required=True, help="WAV del Engine host (HW_WAV=1)")
    ap.add_argument("--track", required=True, help="pista MCI que se oyó (extracted/music/TrackN.wav)")
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--track-seconds", type=float, default=60.0)
    ap.add_argument("--out", help="WAV de la ventana para el oído (48 kHz)")
    a = ap.parse_args()

    mix, mr, mc = read_wav(a.mix)
    track, tr, tc = read_wav(a.track, a.track_seconds)
    window = mix[-int(a.seconds * mr):]
    if a.out:
        write_wav(a.out, window, mr)
    t48 = resample_linear(track, tr, mr)

    print(f"mezcla: {a.mix}  {mr} Hz {mc}ch {len(mix)/mr:.2f}s")
    print(f"pista : {a.track}  {tr} Hz {tc}ch -> remapeada a {mr} Hz ({len(t48)/mr:.1f}s)")
    print(f"ventana: últimos {a.seconds:.1f}s  RMS={20*np.log10(np.sqrt(np.mean(window**2))/32768.0):.2f} dBFS")
    for ch in range(min(mc, tc)):
        k, coef, gain = best_lag(window[:, ch], t48[:, ch])
        print(f"  ch{ch}: lag={k/mr:.3f}s  coef={coef:+.3f}  ganancia_por_proyeccion={gain:+.1f} dB")
    print("coef consistente en ambos canales = la pista esta en la mezcla "
          "(arriba hay efectos no correlacionados, asi que coef < 1).")


if __name__ == "__main__":
    main()
