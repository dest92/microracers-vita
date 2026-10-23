#!/usr/bin/env python3
"""Decompose one captured DSound stream by original WAV bytes and event time."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import wave
import numpy as np
import dsound_oracle as oracle

def decompose(path,game):
    records=oracle.load_trace(path)
    total=sum(w[2] for k,w,d in records if k==oracle.MIX)
    rate=next(w[3] for k,w,d in records if k==oracle.MIX)
    names={}
    for f in game.rglob('*'):
        if f.suffix.lower()!='.wav':continue
        with wave.open(str(f)) as wav:raw=wav.readframes(wav.getnframes())
        names[hashlib.sha256(raw).hexdigest()]=str(f.relative_to(game))
    # Annotate labels using the actual payload, not object IDs that can be reused.
    data={};labels={};events=[];clock=0
    for kind,w,payload in records:
        if kind==oracle.CREATE:data[w[0]]=bytearray(w[2]);labels[w[0]]='primary' if w[3]&1 else 'unknown'
        elif kind==oracle.DUP:data[w[0]]=data[w[1]];labels[w[0]]=labels[w[1]]
        elif kind==oracle.UNLOCK:
            data[w[0]][w[1]:w[1]+w[2]]=payload
            label=names.get(hashlib.sha256(data[w[0]]).hexdigest(),'unknown')
            for obj in data:
                if data[obj] is data[w[0]]:labels[obj]=label
        elif kind==oracle.RELEASE:data.pop(w[0],None)
        if kind in (oracle.PLAY,oracle.STOP,oracle.FREQ,oracle.POS):events.append((clock,kind,labels.get(w[0],'unknown')))
        if kind==oracle.MIX:clock+=w[2]
    stats={};groups={};active=collections.Counter()
    bands=[(0,2000),(2000,4000),(4000,8000),(8000,rate/2)]
    def observe(start,out_rate,v,pcm,valid,reference):
        # Label by the shared byte content (duplicates retain the same storage).
        label=names.get(hashlib.sha256(v['data']).hexdigest(),'unknown')
        key='motor' if 'vehicles' in label.lower() else ('menu' if '/MENU/' in '/'+label.upper() else 'effects')
        if label.upper().endswith('SILENCE.WAV'):key='silence'
        if key not in groups:groups[key]=[np.zeros((total,2)),np.zeros((total,2))]
        frames=len(valid);i=np.arange(frames,dtype=np.int64)[valid]
        phase=v['acc']+i*v['rate'];idx=v['cur']//v['align']+phase//out_rate
        n=len(pcm);idx=idx%n if v['loop'] else np.clip(idx,0,n-1)
        nxt=(idx+1)%n if v['loop'] else np.minimum(idx+1,n-1)
        frac=((phase%out_rate)*((1<<32)//out_rate))>>16
        a=pcm[idx].astype(np.int64);b=pcm[nxt].astype(np.int64)
        linear=a+((b-a)*frac[:,None]>>16)
        def gain(mb):return 0 if mb<=-10000 else int(65536*10**(min(0,mb)/2000)+.5)
        gl=gr=gain(v['vol'])
        if v['pan']>0:gl=gl*gain(-v['pan'])>>16
        if v['pan']<0:gr=gr*gain(v['pan'])>>16
        linear=(linear*np.array([gl,gr])>>16).astype(np.float64)
        target=np.arange(start,start+frames)[valid]
        groups[key][0][target]+=linear;groups[key][1][target]+=reference
        row=stats.setdefault(label,dict(samples=0,signal_sq=0.,error_sq=0.,band_error=[0.]*4,grains=0))
        error=linear-reference
        row['samples']+=linear.size;row['signal_sq']+=float(np.sum(reference**2));row['error_sq']+=float(np.sum(error**2));row['grains']+=1
        full=np.zeros((frames,2));full[valid]=error
        power=np.sum(np.abs(np.fft.rfft(full,axis=0))**2,axis=1)
        freq=np.fft.rfftfreq(frames,1/out_rate)
        for b,(lo,hi) in enumerate(bands):row['band_error'][b]+=float(power[(freq>=lo)&(freq<hi)].sum())
        active[start]+=1
    oracle.reference(records,observer=observe)
    all_actual=sum((v[0] for v in groups.values()),start=np.zeros((total,2)))
    all_ref=sum((v[1] for v in groups.values()),start=np.zeros((total,2)))
    captured=np.concatenate([np.frombuffer(d,dtype='<i2').reshape(-1,2) for k,w,d in records if k==oracle.OUTPUT])
    exact=int(np.sum(np.clip(all_actual,-32768,32767).astype(np.int16)!=captured))
    for label,row in stats.items():
        row['snr_db']=10*np.log10(row['signal_sq']/row['error_sq']) if row['signal_sq'] and row['error_sq'] else None
        row['rms_error']=np.sqrt(row['error_sq']/max(1,row['samples']))
        total_band=sum(row['band_error']);row['band_error_fraction']=[v/total_band if total_band else 0 for v in row.pop('band_error')]
    group_report={}
    for name,(a,r) in groups.items():
        error=a-r
        power=np.sum(np.abs(np.fft.rfft(error,axis=0))**2,axis=1);freq=np.fft.rfftfreq(total,1/rate)
        group_report[name]=oracle.metrics(a,r,rate)
        group_report[name]['band_error_fraction']=[float(power[(freq>=lo)&(freq<hi)].sum()/power.sum()) if power.sum() else 0 for lo,hi in bands]
    residual=np.clip(all_actual,-32768,32767)-np.clip(all_ref,-32768,32767)
    delta=np.max(np.abs(np.diff(residual,axis=0)),axis=1)
    indices=np.argsort(delta)[-20:][::-1]
    peaks=[]
    for i in indices:
        event=min(events,key=lambda e:abs(e[0]-int(i))) if events else None
        peaks.append(dict(frame=int(i),seconds=float(i/rate),residual_step=float(delta[i]),nearest_event=dict(frame=event[0],kind=event[1],buffer=event[2],distance_ms=abs(event[0]-int(i))*1000/rate) if event else None))
    block=round(rate*.025);windows=[]
    for i in range(0,total,block):
        windows.append(dict(start_s=i/rate,error_rms=float(np.sqrt(np.mean(residual[i:i+block]**2)))))
    return dict(active_voices_mean=float(np.mean(list(active.values()))),active_voices_p95=float(np.percentile(list(active.values()),95)),active_voices_max=max(active.values()),rate=rate,frames=total,linear_reconstruction_mismatches=exact,bands_hz=bands,
                buffers=stats,groups=group_report,actual_saturated=int(np.sum((all_actual>32767)|(all_actual< -32768))),
                reference_saturated=int(np.sum((all_ref>32767)|(all_ref< -32768))),
                residual_peaks=peaks,windows_25ms=windows,
                note='Music is a separate MCI source, absent from DSound trace. Bands alone do not distinguish imaging from aliasing; event proximity is correlation.')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path);p.add_argument('--game',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    a=p.parse_args();result=decompose(a.trace,a.game);a.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('buffers','windows_25ms','residual_peaks')},indent=2))
