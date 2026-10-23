#!/usr/bin/env python3
"""SPEC-048: A/B del contrato de cursores sobre una captura real.

El juego no consulta GetCurrentPosition (0 llamadas en la captura): el write
cursor se le entrega en el Lock (offset devuelto = write_cursor(play cursor)).
Cada evento LOCK de la captura trae el offset REAL que recibió el guest con la
fórmula vieja, así que de ahí sale el play cursor:

    play = (offset - lead_viejo) mod len

Con ese cursor se evalúan las dos fórmulas sobre los MISMOS datos del juego:
  - la vieja (min(2*kGrain, len/4)): ¿la escritura caía dentro de la ventana
    que el mixer iba a leer? (el guest pisa lo no leído -> click);
  - la nueva (ventana de lectura + un grano de holgura, escalada por la
    frecuencia de la voz): ¿sigue solapando?
  - y cuánto espacio de relleno le queda al guest (len - lead) en ms de audio:
    el costo de latencia del cambio.

Es una medición sobre los datos del juego (nivel: captura real, host). No
modifica la captura ni ejecuta el juego. OJO: el replay offline NO puede medir
el efecto audible, porque el trace fija los offsets de Lock grabados; el A/B de
audio necesita una corrida real del juego.
"""
import argparse
import importlib.util
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('dsound_oracle', ROOT / 'tools/dsound_oracle.py')
mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mod)
CREATE, LOCK, FREQ = 1, 4, 6

K_GRAIN = 512      # frames de salida por grano (ds_emul kGrain)
K_RATE = 22050     # kRate del guest


def lead_old(length, block_align):
    """La fórmula que usaba ds_emul antes del contrato (winx86-0014)."""
    lead = (2 * K_GRAIN) * block_align
    if lead > length // 4:
        lead = length // 4
    if lead < block_align:
        lead = block_align
    return lead


def read_window_frames(rate):
    """Frames de fuente que el mixer lee en un grano (ceil + 1 de look-ahead)."""
    return -(-(K_GRAIN * rate) // K_RATE) + 1


def lead_new(length, block_align, rate):
    """La fórmula del contrato: ventana + un grano de holgura, acotado."""
    if not length or not block_align:
        return 0
    advance = (K_GRAIN * rate) // K_RATE
    lead = (read_window_frames(rate) + advance) * block_align
    hi = length - block_align if length > block_align else block_align
    return min(lead, hi)


def intersects(start, count, window_start, window_len, length):
    """¿El tramo [start, start+count) (mod length) toca la ventana del mixer?"""
    if count <= 0 or window_len <= 0:
        return False
    d = (start - window_start) % length
    return d < window_len or (d + count) > length


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--trace', required=True, help='captura HWDS001 (dsound.bin)')
    ap.add_argument('--out', help='JSON de salida')
    a = ap.parse_args()

    records = mod.load_trace(Path(a.trace))
    voices = {}
    locks = overlap_old = overlap_new = locks_nonzero_off = 0
    fill_old_ms, fill_new_ms = [], []
    room_new = []          # (room_viejo, room_nuevo) en bytes, por lock
    worst = []
    for kind, words, _payload in records:
        if kind == CREATE:
            obj, _va, length, _x, _fmt, _bits, rate, align = words[:8]
            voices[obj] = {'len': length, 'align': align or 4,
                           'fmt_rate': rate or K_RATE, 'rate': rate or K_RATE}
        elif kind == FREQ:
            obj, rate = words
            if obj in voices and rate:
                voices[obj]['rate'] = rate
        elif kind == LOCK:
            obj, off, b1, _zero, b2 = words
            v = voices.get(obj)
            if not v or not v['len']:
                continue
            length, align, rate = v['len'], v['align'], v['rate']
            old = lead_old(length, align)
            new = lead_new(length, align, rate)
            play = (off - old) % length          # el cursor real del mixer
            window = read_window_frames(rate) * align
            written = b1 + b2
            locks += 1
            if intersects(off, written, play, window, length):
                overlap_old += 1
            new_off = (play + new) % length
            if intersects(new_off, written, play, window, length):
                overlap_new += 1
            fill_old_ms.append((length - old) / (v['fmt_rate'] * align) * 1000.0)
            fill_new_ms.append((length - new) / (v['fmt_rate'] * align) * 1000.0)
            room_new.append((length - old, length - new,
                             (length - new) / (v['fmt_rate'] * align) * 1000.0))
            if off:
                locks_nonzero_off += 1
            worst.append((length, rate, old, new, window, written))

    def stat(values):
        if not values:
            return {}
        return {'min': round(min(values), 2), 'mediana': round(statistics.median(values), 2),
                'max': round(max(values), 2)}

    # ¿Qué buffers podrían hacer streaming? El guest necesita poder escribir al
    # menos un grano de fuente (kGrain*block_align) por ciclo. Si con la fórmula
    # vieja el room ya es menor que un grano, ese buffer es de un solo llenado
    # y el lead no le restringe nada.
    grain_bytes = K_GRAIN * 4
    streaming = [r for r in room_new if r[0] >= grain_bytes]
    report = {
        'trace': str(a.trace),
        'voces': len(voices),
        'locks': locks,
        'locks_con_offset_distinto_de_0': locks_nonzero_off,
        'solape_ventana_viejo': overlap_old,
        'solape_ventana_nuevo': overlap_new,
        'relleno_ms_viejo': stat(fill_old_ms),
        'relleno_ms_nuevo': stat(fill_new_ms),
        'buffers_que_pueden_hacer_streaming': len(streaming),
        'room_nuevo_min_ms_en_los_que_pueden_streaming':
            round(min((r[2] for r in streaming), default=0.0), 2),
        'casos_peores': [
            {'len': l, 'rate': r, 'lead_viejo': o, 'lead_nuevo': n, 'ventana': w, 'bytes_escritos': b}
            for l, r, o, n, w, b in sorted(worst, key=lambda x: x[3] - x[2], reverse=True)[:5]
        ],
    }
    for key, value in report.items():
        print(f'{key}: {value}')
    if a.out:
        import json
        Path(a.out).write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(f'JSON: {a.out}')


if __name__ == '__main__':
    main()
