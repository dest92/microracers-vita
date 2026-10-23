#!/usr/bin/env python3
"""Build offline prototypes from the exact production kernel: cubic / Q15 sinc.
Generated tables are diagnostic artifacts. No game data is embedded.
"""
import argparse
from pathlib import Path
import subprocess
import numpy as np

CUBIC='''        if (f) {
            auto at = [&](int delta, int channel) -> int32_t {
                int64_t p = int64_t(v.pos) + int64_t(delta) * v.blockAlign;
                if (p < 0 || uint64_t(p) + width > v.len) {
                    if (v.loop) { p %= v.len; if (p < 0) p += v.len; }
                    else p = p < 0 ? 0 : v.len - v.blockAlign;
                }
                return rd(uint32_t(p), channel);
            };
            auto cubic = [&](int channel, int32_t p1) -> int32_t {
                const int64_t p0=at(-1,channel), p2=at(1,channel), p3=at(2,channel);
                const int64_t a0=-p0+3*p1-3*p2+p3;
                const int64_t a1=2*p0-5*p1+4*p2-p3;
                const int64_t a2=-p0+p2;
                return p1+int32_t((f*(a2+((f*(a1+((f*a0)>>16)))>>16)))>>17);
            };
            l0=cubic(0,l0); r0=v.ch==2?cubic(1,r0):l0;
        }
'''

def header(base,kind,dst):
    begin=base.index('        if (f) {');end=base.index('        out[2 * i]',begin)
    if kind=='cubic':replacement=CUBIC
    else:
        taps=int(kind[4:]);half=taps//2
        table=[]
        for bank in range(32):
            cutoff=(32-bank)/32
            rows=[]
            for phase in range(256):
                distance=phase/256-np.arange(-half+1,half+1)
                coef=cutoff*np.sinc(cutoff*distance)*np.sinc(distance/half)
                coef[np.abs(distance)>=half]=0
                coef/=coef.sum();q=np.clip(np.rint(coef*32768),-32768,32767).astype(np.int32)
                correction=32768-int(q.sum())
                # Keep every coefficient signed Q15 while preserving DC.
                for idx in np.argsort(-np.abs(coef)):
                    delta=int(np.clip(correction,-32768-q[idx],32767-q[idx]));q[idx]+=delta;correction-=delta
                    if not correction:break
                assert q.sum()==32768
                rows.append('{'+','.join(map(str,q))+'}')
            table.append('{'+',\n'.join(rows)+'}')
        (dst/'runtime/ds_coefficients.h').write_text(f'alignas(32) static const int16_t hw_ds_coeff[32][256][{taps}]={{'+',\n'.join(table)+'};\n')
        base=base.replace('#include <cstring>','#include <cstring>\n#include "ds_coefficients.h"')
        base=base.replace('    auto rd =', '    const uint32_t raw_bank=v.rate<=out_rate?0:32-uint32_t(uint64_t(out_rate)*32/v.rate);\n    const uint32_t hw_bank=raw_bank<32?raw_bank:31;\n    auto rd =')
        begin=base.index('        if (f) {');end=base.index('        out[2 * i]',begin)
        replacement=f'''        {{
            // Conservative antialias cutoff, quantized to 1/32. Table Q15.
            const uint32_t bank=hw_bank;
            const int16_t* weights=hw_ds_coeff[bank][uint32_t(f)>>8];
            auto filtered = [&](int channel) -> int32_t {{
                int64_t sum=0;
                for(int tap=0;tap<{taps};++tap) {{
                    int64_t p=int64_t(v.pos)+int64_t(tap-{half-1})*v.blockAlign;
                    if(p<0||uint64_t(p)+width>v.len) {{
                        if(v.loop) {{p%=v.len;if(p<0)p+=v.len;}}
                        else p=p<0?0:v.len-v.blockAlign;
                    }}
                    sum+=int64_t(rd(uint32_t(p),channel))*weights[tap];
                }}
                return int32_t(sum>>15);
            }};
            if(f||bank) {{l0=filtered(0);r0=v.ch==2?filtered(1):l0;}}
        }}
'''
    return base[:begin]+replacement+base[end:]

def build(root,wx86,out):
    base=(wx86/'src/runtime/ds_resample.h').read_text()
    for kind in ['linear','cubic','sinc8','sinc16']:
        dst=out/kind;(dst/'runtime').mkdir(parents=True,exist_ok=True)
        text=base if kind=='linear' else header(base,kind,dst)
        (dst/'runtime/ds_resample.h').write_text(text)
        subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(dst),'-I'+str(root/'src'),str(root/'tools/dsound_replay.cpp'),'-o',str(dst/'replay')],check=True)
        subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(dst),str(root/'tools/dsound_resample_bench.cpp'),'-o',str(dst/'bench')],check=True)
        print(kind,dst,flush=True)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--winx86',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();build(Path(__file__).resolve().parents[1],a.winx86.resolve(),a.out.resolve())
