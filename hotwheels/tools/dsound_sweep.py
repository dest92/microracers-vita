#!/usr/bin/env python3
"""One captured call stream, several production-kernel prototypes, same window."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import numpy as np
import dsound_oracle as oracle

def sweep(trace,variants,out):
    records=oracle.load_trace(trace);ref=oracle.reference(records)
    rate=next(w[3] for k,w,d in records if k==oracle.MIX)
    result={};out.mkdir(parents=True,exist_ok=True)
    start=max(0,len(ref)-6*rate)
    oracle.write_wav(out/'referencia-sinc64.wav',ref[start:],rate)
    with tempfile.TemporaryDirectory(prefix='hw069-sweep-') as td:
        for kind in ['linear','cubic','sinc8','sinc16']:
            raw=Path(td)/(kind+'.s16')
            run=subprocess.run([str(variants/kind/'replay'),str(trace),str(raw)],capture_output=True,text=True,check=True)
            pcm=np.fromfile(raw,dtype='<i2').reshape(-1,2)
            result[kind]=oracle.metrics(pcm,ref,rate)
            result[kind]['native_report']=run.stdout.strip()
            result[kind]['host_cost']=subprocess.check_output([str(variants/kind/'bench')],text=True).strip()
            oracle.write_wav(out/(kind+'.wav'),pcm[start:],rate)
    result['window']=dict(start_frame=start,frames=len(ref)-start,rate=rate,seconds=(len(ref)-start)/rate,music=False)
    (out/'sweep.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path);p.add_argument('--variants',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();sweep(a.trace.resolve(),a.variants.resolve(),a.out.resolve())
