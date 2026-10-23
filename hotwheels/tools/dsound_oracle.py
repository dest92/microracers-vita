#!/usr/bin/env python3
"""SPEC-048 offline DSound oracle. Game PCM is never a source artifact.

Native replay uses ds_emul's kernel; independent reference uses float64
windowed sinc (64 taps, per-voice antialias cutoff) and DirectSound dB/pan.
Capture is intrusive: it measures fidelity, never realtime performance.
"""
import argparse
import copy
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import wave
import numpy as np

CREATE,DUP,RELEASE,LOCK,UNLOCK,FREQ,VOL,PAN,POS,PLAY,STOP,GETPOS,FORMAT,MIX,OUTPUT,END=range(1,17)
COUNTS={CREATE:(8,9),DUP:(2,),RELEASE:(1,),LOCK:(5,),UNLOCK:(3,),FREQ:(2,),VOL:(2,),PAN:(2,),POS:(2,),PLAY:(2,),STOP:(1,),GETPOS:(3,),FORMAT:(5,),MIX:(4,),OUTPUT:(4,),END:(1,)}

def load_trace(path):
    data=Path(path).read_bytes()
    if len(data)>128*1024*1024 or data[:8]!=b'HWDS001\0':raise ValueError('invalid trace magic/size')
    at=8;records=[];ended=False
    while at<len(data):
        if at+12>len(data):raise ValueError('truncated record')
        kind,n,size=struct.unpack_from('<III',data,at);at+=12
        if kind not in COUNTS or n not in COUNTS[kind] or size>4*1024*1024:raise ValueError('invalid record header')
        if at+n*4+size>len(data):raise ValueError('truncated payload')
        words=list(struct.unpack_from('<'+'I'*n,data,at));at+=4*n
        payload=data[at:at+size];at+=size
        if ended:raise ValueError('bytes after END')
        if kind not in (UNLOCK,OUTPUT) and size:raise ValueError('unexpected payload')
        if kind==END:
            if words[0]!=0:raise ValueError('trace capped/incomplete')
            ended=True
        records.append((kind,words,payload))
    if not ended:raise ValueError('missing END: capture incomplete')
    return records

def signed(v):return v if v<2**31 else v-2**32

def amplitude(mb,mutant=None):
    if mb<=-10000:return 0.
    return 10.**(min(0,mb)/(1000. if mutant=='db_power' else 2000.))

def interpolate(pcm,x,step,loop,mutant):
    n=len(pcm)
    if mutant=='nearest':return pcm[np.floor(x).astype(np.int64)%n]
    center=np.floor(x).astype(np.int64)
    offsets=np.arange(-31,33)
    indices=center[:,None]+offsets[None,:]
    distance=x[:,None]-indices
    cutoff=min(1.,1./step)
    # Lanczos-windowed low pass. Normalize DC per output sample.
    weights=cutoff*np.sinc(cutoff*distance)*np.sinc(distance/32.)
    weights[np.abs(distance)>=32]=0
    weights/=weights.sum(axis=1,keepdims=True)
    indices=indices%n if loop else np.clip(indices,0,n-1)
    return np.sum(pcm[indices]*weights[:,:,None],axis=1)

def reference(records,mutant=None,observer=None):
    voices={};chunks=[];timeline=0;allocated=0
    for kind,w,data in records:
        if kind==CREATE:
            obj,_,size,flags,ch,bits,rate,align=w[:8]
            if bits not in (8,16) or ch not in (1,2) or align!=ch*bits//8 or not 100<=rate<=200000 or size%align or not size or (len(w)==9 and w[8]!=1):raise ValueError('unsupported PCM format')
            allocated+=size
            if allocated>128*1024*1024 or len(voices)>4096:raise ValueError('trace memory limit')
            voices[obj]=dict(data=bytearray(size),flags=flags,ch=ch,bits=bits,rate=rate,fmt=rate,align=align,cur=0,acc=0,vol=0,pan=0,playing=False,loop=False)
        elif kind==DUP:
            if w[1] not in voices:raise ValueError('unknown duplicate source')
            voices[w[0]]=copy.copy(voices[w[1]])
            voices[w[0]].update(cur=0,acc=0,playing=False,loop=False)
        elif kind==RELEASE:
            if voices.pop(w[0],None) is None:raise ValueError('unknown release')
        elif kind in (LOCK,UNLOCK,FREQ,VOL,PAN,POS,PLAY,STOP,GETPOS,FORMAT):
            if w[0] not in voices:raise ValueError('unknown voice')
            v=voices[w[0]]
            if kind==UNLOCK:
                if len(data)!=w[2] or w[1]>len(v['data']) or w[2]>len(v['data'])-w[1]:raise ValueError('Unlock payload/range invalid')
                v['data'][w[1]:w[1]+w[2]]=data
            elif kind==FREQ:
                if w[1] and not 100<=w[1]<=200000:raise ValueError('invalid frequency')
                v['rate']=w[1] or v['fmt']
            elif kind==VOL:v['vol']=signed(w[1])
            elif kind==PAN:v['pan']=signed(w[1])
            elif kind==POS:v.update(cur=w[1]%len(v['data']),acc=0)
            elif kind==PLAY:v.update(playing=True,loop=bool(w[1]&1))
            elif kind==STOP:v['playing']=False
            elif kind==FORMAT:
                if w[2]!=v['bits'] or w[1]!=v['ch'] or w[4]!=v['align'] or not w[3]:raise ValueError('unsupported format transition')
                v['rate']=v['fmt']=w[3]
        elif kind==MIX:
            start=w[0]+(w[1]<<32);frames,rate=w[2:]
            if start!=timeline or not 0<frames<=512 or not 1000<=rate<=192000:raise ValueError('invalid mix timeline')
            mix=np.zeros((frames,2),np.float64)
            for v in voices.values():
                if not v['playing'] or v['flags']&1:continue
                pcm=np.frombuffer(v['data'],dtype='<i2' if v['bits']==16 else np.uint8).astype(np.float64)
                if v['bits']==8:pcm=(pcm-128)*256
                pcm=pcm.reshape(-1,v['ch'])
                if v['ch']==1:pcm=pcm.repeat(2,axis=1)
                acc=0 if mutant=='reset_phase' else v['acc']
                x=v['cur']/v['align']+(acc+np.arange(frames)*v['rate'])/rate
                valid=np.ones(frames,dtype=bool) if v['loop'] else x<len(pcm)
                samples=interpolate(pcm,x[valid],v['rate']/rate,v['loop'],mutant)
                base=amplitude(v['vol'],mutant)
                gains=[base*amplitude(-max(0,v['pan']),mutant),base*amplitude(min(0,v['pan']),mutant)]
                contribution=samples*np.array(gains)
                mix[valid]+=contribution
                if observer:observer(timeline,rate,v,pcm,valid,contribution)
                advance,rem=divmod(acc+frames*v['rate'],rate)
                pos=v['cur']+advance*v['align'];v['acc']=rem
                if v['loop']:v['cur']=pos%len(v['data'])
                elif pos>=len(v['data']):v.update(cur=len(v['data'])-v['align'],acc=0,playing=False)
                else:v['cur']=pos
            chunks.append(np.clip(mix,-32768,32767));timeline+=frames
    return np.concatenate(chunks) if chunks else np.zeros((0,2))

def metrics(actual,ref,rate,tone_hz=None):
    actual=np.asarray(actual,dtype=np.float64);ref=np.asarray(ref,dtype=np.float64)
    if actual.shape!=ref.shape or not actual.size:raise ValueError('empty/mismatched audio')
    error=actual-ref
    power=float(np.mean(ref**2));noise=float(np.mean(error**2))
    snr=10*math.log10(power/noise) if power>0 and noise>0 else (999. if not noise else -999.)
    mono=actual.mean(axis=1);err=error.mean(axis=1)
    window=np.hanning(len(mono))
    spectrum=np.abs(np.fft.rfft(mono*window))**2
    error_spectrum=np.abs(np.fft.rfft(err*window))**2
    freq=np.fft.rfftfreq(len(mono),1./rate)
    total=float(spectrum.sum())
    result=dict(snr_db=snr,rms=float(np.sqrt(np.mean(actual**2))),reference_rms=float(np.sqrt(power)),
                error_rms=float(np.sqrt(noise)),peak=float(np.max(np.abs(actual))),
                clipped_samples=int(np.sum((actual>=32767)|(actual<=-32768))),
                reference_clipped_samples=int(np.sum((ref>=32767)|(ref<=-32768))),
                discontinuities_gt_16384=int(np.sum(np.abs(np.diff(actual,axis=0))>16384)),
                max_step=float(np.max(np.abs(np.diff(actual,axis=0)))) if len(actual)>1 else 0.,
                highband_fraction=float(spectrum[freq>=rate*.4].sum()/total) if total else 0.,
                highband_error_fraction=float(error_spectrum[freq>=rate*.4].sum()/total) if total else 0.,thd_db=None)
    if tone_hz:
        if not 0<tone_hz<rate/2:raise ValueError('invalid tone frequency')
        df=rate/len(mono)
        def band(hz):return float(spectrum[np.abs(freq-hz)<=1.1*df].sum())
        fundamental=band(tone_hz)
        harmonics=sum(band(k*tone_hz) for k in range(2,11) if k*tone_hz<rate/2)
        result['thd_db']=10*math.log10(harmonics/fundamental) if fundamental and harmonics else -999.
    return result

def write_wav(path,pcm,rate):
    if os.environ.get('HW_WAV')!='1':raise ValueError('WAV export requires HW_WAV=1')
    with wave.open(str(path),'wb') as f:
        f.setnchannels(2);f.setsampwidth(2);f.setframerate(rate)
        f.writeframes(np.clip(np.rint(pcm),-32768,32767).astype('<i2').tobytes())

def compare_final(records,before_runner,after_runner,wav_prefix=None,max_wav_seconds=8.):
    try:
        from scipy.signal import resample_poly
    except ImportError as exc:raise ValueError("final comparison requires scipy") from exc
    chunks=sorted((w[0]+(w[1]<<32),data) for k,w,data in records if k==OUTPUT)
    source=np.frombuffer(b''.join(data for _,data in chunks),dtype='<i2').reshape(-1,2)
    # Rational 22050->48000 = 320/147. scipy: 20*max(up,down)+1 FIR
    # coefficients, Kaiser beta=8.6, zero extension at the outer edges.
    ref=np.clip(resample_poly(source.astype(np.float64),320,147,axis=0,window=('kaiser',8.6)),-32768,32767)
    streams={}
    with tempfile.TemporaryDirectory(prefix='hw069-final-') as td:
        pcm=Path(td)/'source.s16';pcm.write_bytes(source.tobytes())
        for name,runner in [('before',before_runner),('after',after_runner)]:
            out=Path(td)/(name+'.s16')
            proc=subprocess.run([str(runner),str(pcm),str(out)],capture_output=True,text=True)
            if proc.returncode:raise ValueError(proc.stderr.strip() or 'final replay failed')
            streams[name]=np.fromfile(out,dtype='<i2').reshape(-1,2)
    n=min(len(ref),*(len(s) for s in streams.values()))
    if n<512:raise ValueError('too few final-stage frames')
    # Exclude FIR padding/transients at capture edges, not internal boundaries.
    lo,hi=128,n-128
    report={name:metrics(pcm[lo:hi],ref[lo:hi],48000) for name,pcm in streams.items()}
    delta=report['after']['snr_db']-report['before']['snr_db']
    report.update(snr_improvement_db=delta,frames=hi-lo,rate=48000,source_read_frames=512,
                  changed_samples=int(np.sum(streams['before'][:n]!=streams['after'][:n])),
                  decision='mejora medida' if delta>=.1 and report['after']['clipped_samples']<=report['before']['clipped_samples'] else 'no separable / revisar',
                  reference='scipy resample_poly 320/147 Kaiser beta8.6; exclude 128 edge frames')
    if wav_prefix:
        count=min(hi-lo,int(max_wav_seconds*48000));start=hi-count
        for name,pcm in streams.items():write_wav(str(wav_prefix)+'-final-'+name+'.wav',pcm[start:hi],48000)
        write_wav(str(wav_prefix)+'-final-sinc.wav',ref[start:hi],48000)
    return report

def analyze(path,native_runner,wav_prefix=None,max_wav_seconds=8.,final_before=None,final_after=None):
    records=load_trace(path)
    ref=reference(records)
    captured={};mixes=[];formats=set();playing={};overlap=0;freqs=[];rates=set();lock_active=set();mix_during_lock=0
    for kind,w,data in records:
        if kind==CREATE:formats.add((w[5],w[4],w[6]));playing[w[0]]=False
        elif kind==DUP:playing[w[0]]=False
        elif kind==RELEASE:playing.pop(w[0],None);lock_active.discard(w[0])
        elif kind==PLAY:playing[w[0]]=True
        elif kind==STOP:playing[w[0]]=False
        elif kind==LOCK:lock_active.add(w[0]);overlap+=bool(playing.get(w[0]))
        elif kind==UNLOCK:lock_active.discard(w[0])
        elif kind==FREQ:freqs.append(w[1])
        elif kind==MIX:
            mixes.append((w[0]+(w[1]<<32),w[2]));rates.add(w[3]);mix_during_lock+=len(lock_active)
        elif kind==OUTPUT:
            key=w[0]+(w[1]<<32)
            if key in captured or len(data)!=w[2]*4:raise ValueError('duplicate/invalid Output')
            captured[key]=np.frombuffer(data,dtype='<i2').reshape(-1,2)
    if len(rates)!=1 or len(mixes)!=len(captured):raise ValueError('missing Output or changing output rate')
    try:actual=np.concatenate([captured[start] if len(captured[start])==n else (_ for _ in ()).throw(ValueError('Output frames')) for start,n in mixes])
    except KeyError as e:raise ValueError('missing Output index') from e
    with tempfile.TemporaryDirectory(prefix='hw069-replay-') as td:
        out=Path(td)/'native.s16'
        proc=subprocess.run([str(native_runner),str(path),str(out)],capture_output=True,text=True)
        if proc.returncode:raise ValueError(proc.stderr.strip())
        native=np.fromfile(out,dtype='<i2').reshape(-1,2)
    if native.shape!=actual.shape:raise ValueError('native/capture frame count differs')
    rate=next(iter(rates));report=metrics(actual,ref,rate)
    cursor_match=re.search(r'cursor_mismatches=(\d+)',proc.stdout)
    if not cursor_match:raise ValueError('native replay did not report cursor counters')
    report.update(frames=len(actual),rate=rate,seconds=len(actual)/rate,
                  exact_mismatches=int(np.sum(native!=actual)),exact_max_error=int(np.max(np.abs(native.astype(np.int32)-actual))),
                  cursor_mismatches=int(cursor_match.group(1)),
                  formats=[dict(bits=b,channels=c,rate=r) for b,c,r in sorted(formats)],
                  set_frequency_calls=len(freqs),frequency_range=[min(freqs),max(freqs)] if freqs else [],
                  unlock_while_playing=overlap,mixes_during_lock=mix_during_lock,
                  note='THD only for known-tone fixtures. High-band error is not proof of aliasing. Trace is intrusive.')
    if wav_prefix:
        # Select the final active window; keep both versions sample-aligned.
        count=min(len(actual),int(max_wav_seconds*rate));start=len(actual)-count
        write_wav(str(wav_prefix)+'-ds-emul.wav',actual[start:],rate)
        write_wav(str(wav_prefix)+'-sinc.wav',ref[start:],rate)
        report['wav_start_frame']=start
    if final_before or final_after:
        if not final_before or not final_after or rate!=22050:raise ValueError('final comparison requires both runners and 22050 Hz source')
        report['final_stage']=compare_final(records,final_before,final_after,wav_prefix,max_wav_seconds)
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace',type=Path);p.add_argument('--native',type=Path,default=Path(__file__).resolve().parents[1]/'build/dsound_replay')
    p.add_argument('--report',type=Path);p.add_argument('--wav-prefix',type=Path);p.add_argument('--seconds',type=float,default=8.)
    p.add_argument('--final-before',type=Path);p.add_argument('--final-after',type=Path)
    a=p.parse_args()
    try:report=analyze(a.trace,a.native,a.wav_prefix,a.seconds,a.final_before,a.final_after)
    except (ValueError,OSError) as e:p.exit(1,f'oracle: {e}\n')
    result=json.dumps(report,indent=2,ensure_ascii=False)+'\n'
    if a.report:a.report.write_text(result)
    print(result,end='')
    return 0 if report['exact_mismatches']==0 and report['cursor_mismatches']==0 else 1
if __name__=='__main__':raise SystemExit(main())
