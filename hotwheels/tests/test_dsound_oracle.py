#!/usr/bin/env python3
"""SPEC-048: independent trace fixtures, corruption checks and audio metrics."""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('dsound_oracle', ROOT/'tools/dsound_oracle.py')
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

CREATE,DUP,RELEASE,LOCK,UNLOCK,FREQ,VOL,PAN,POS,PLAY,STOP,GETPOS,FORMAT,MIX,OUTPUT,END = range(1,17)

# El binario de replay es un insumo externo (lo construye tests/run_dsound_oracle.sh).
# Sin HW_DS_REPLAY esos tests se SALTAN, no fallan: una suite que necesita un
# insumo no versionado no puede dejar run_all en rojo (BUG-048; precedente:
# test_qemu_insn_oracle.py).
REPLAY = os.environ.get('HW_DS_REPLAY')
needs_replay = unittest.skipUnless(REPLAY, 'requiere HW_DS_REPLAY (binario de replay)')

def rec(kind, words=(), data=b''):
    return struct.pack('<III',kind,len(words),len(data))+struct.pack('<'+'I'*len(words),*(v&0xffffffff for v in words))+data

def fixture(bits=16):
    pcm=np.array([0,1000,-2000,3000,-4000,5000,6000,-7000],dtype='<i2')
    payload=pcm.tobytes() if bits==16 else bytes([128,255,0,192,64,129,127,128])
    align=bits//8
    gain=int(65536*10**(-600/2000)+.5)
    expected=np.repeat(((pcm.astype(np.int64)*gain)>>16)[:,None],2,axis=1).astype('<i2')
    b=b'HWDS001\0'+rec(CREATE,[1,0x1000,len(payload),0,1,bits,22050,align])
    # Unlock wraps into two segments, modeled as separate payload records.
    b+=rec(LOCK,[1,0,6,6,len(payload)-6])+rec(UNLOCK,[1,0,6],payload[:6])+rec(UNLOCK,[1,6,len(payload)-6],payload[6:])
    b+=rec(VOL,[1,-600])+rec(PAN,[1,0])+rec(PLAY,[1,0])+rec(MIX,[0,0,8,22050])
    b+=rec(OUTPUT,[0,0,8,22050],expected.tobytes())+rec(GETPOS,[1,7*align,align])
    b+=rec(DUP,[2,1])+rec(RELEASE,[1])+rec(POS,[2,0])+rec(VOL,[2,-10000])+rec(PAN,[2,10000])+rec(FREQ,[2,11025])+rec(PLAY,[2,1])
    b+=rec(MIX,[8,0,16,22050])+rec(OUTPUT,[8,0,16,22050],bytes(64))+rec(STOP,[2])+rec(RELEASE,[2])+rec(END,[0])
    return b

class Oracle(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.path=Path(self.tmp.name)/'trace.bin';self.path.write_bytes(fixture())
    @needs_replay
    def test_exact_native_and_reference(self):
        report=mod.analyze(self.path,Path(REPLAY))
        self.assertEqual(report['exact_mismatches'],0)
        self.assertGreater(report['snr_db'],65)  # float reference vs Q16 gain/floor (<1 PCM RMS)
        self.assertLess(report['error_rms'],1)
        self.assertEqual(report['cursor_mismatches'],0)
        self.assertEqual(report['frames'],24)
        self.assertEqual(report['formats'],[{'bits':16,'channels':1,'rate':22050}])
        self.assertEqual(report['unlock_while_playing'],0)
    def test_truncated_and_capped_are_errors(self):
        for data in [fixture()[:-1],fixture()[:-20],b'badmagic',fixture().replace(rec(END,[0]),rec(END,[1]))]:
            self.path.write_bytes(data)
            with self.assertRaises(ValueError):mod.load_trace(self.path)
    @needs_replay
    def test_invalid_payload_not_silently_accepted(self):
        data=b'HWDS001\0'+rec(CREATE,[1,0,4,0,1,16,22050,2])+rec(UNLOCK,[1,3,4],bytes(4))+rec(END,[0])
        self.path.write_bytes(data)
        with self.assertRaises(ValueError):mod.analyze(self.path,Path(REPLAY))
    @needs_replay
    def test_8bit_is_explicitly_unsupported_by_runtime(self):
        self.path.write_bytes(fixture(8))
        with self.assertRaisesRegex(ValueError,'8|format|PCM'):mod.analyze(self.path,Path(REPLAY))
    def test_metrics_and_thd_on_tone(self):
        rate=48000;hz=1000
        t=np.arange(rate)/rate
        clean=10000*np.sin(2*np.pi*hz*t)
        dirty=clean+1000*np.sin(2*np.pi*2*hz*t)
        m=mod.metrics(dirty[:,None].repeat(2,axis=1),clean[:,None].repeat(2,axis=1),rate,hz)
        self.assertAlmostEqual(m['snr_db'],20,places=2)
        self.assertAlmostEqual(m['thd_db'],-20,places=1)
        self.assertAlmostEqual(m['rms'],np.sqrt(10000**2/2+1000**2/2),places=1)
    def test_sinc_rejects_nearest_and_phase_reset(self):
        n=4096;rate=22050
        pcm=(12000*np.sin(2*np.pi*41*np.arange(n)/n)).astype('<i2')
        b=b'HWDS001\0'+rec(CREATE,[1,0,n*2,0,1,16,rate,2])+rec(UNLOCK,[1,0,n*2],pcm.tobytes())+rec(PLAY,[1,1])
        for k in range(20):
            b+=rec(FREQ,[1,10003+k*613])+rec(MIX,[k*127,0,127,rate])
        b+=rec(END,[0]);self.path.write_bytes(b)
        records=mod.load_trace(self.path)
        good=mod.reference(records)
        for mutant in ['nearest','reset_phase','db_power']:
            # db mutant needs non-unity volume to be observable.
            mutated_records=list(records)
            mutated_records.insert(2,(VOL,[1,0xfffffda8],b''))
            ref=mod.reference(mutated_records)
            bad=mod.reference(mutated_records,mutant=mutant)
            self.assertGreater(np.sqrt(np.mean((ref-bad)**2)),10,mutant)
        self.assertGreater(float(np.max(np.abs(good))),1000)
    @needs_replay
    def test_native_fidelity(self):
        n=4096;rate=22050
        pcm=(12000*np.sin(2*np.pi*41*np.arange(n)/n)).astype('<i2')
        b=b'HWDS001\0'+rec(CREATE,[1,0,n*2,0,1,16,rate,2])+rec(UNLOCK,[1,0,n*2],pcm.tobytes())+rec(VOL,[1,-600])+rec(PLAY,[1,1])
        for k in range(20):b+=rec(FREQ,[1,10003+k*613])+rec(MIX,[k*127,0,127,rate])
        b+=rec(END,[0]);self.path.write_bytes(b)
        out=Path(self.tmp.name)/'native.s16'
        subprocess.run([REPLAY,str(self.path),str(out)],check=True,capture_output=True)
        actual=np.fromfile(out,dtype='<i2').reshape(-1,2)
        report=mod.metrics(actual,mod.reference(mod.load_trace(self.path)),rate)
        self.assertGreater(report['snr_db'],55)

    @needs_replay
    def test_stereo_pan_sum_and_clipping(self):
        pcm=np.array([[30000,-30000]]*16,dtype='<i2')
        b=b'HWDS001\0'
        for obj in [1,2]:
            b+=rec(CREATE,[obj,0,len(pcm.tobytes()),0,2,16,22050,4])+rec(UNLOCK,[obj,0,len(pcm.tobytes())],pcm.tobytes())+rec(PAN,[obj,-10000])+rec(PLAY,[obj,1])
        expect=np.array([[32767,0]]*16,dtype='<i2')
        b+=rec(MIX,[0,0,16,22050])+rec(OUTPUT,[0,0,16,22050],expect.tobytes())+rec(END,[0])
        self.path.write_bytes(b)
        report=mod.analyze(self.path,Path(REPLAY))
        self.assertEqual(report['exact_mismatches'],0)
        self.assertEqual(report['clipped_samples'],16)

    def test_wav_requires_opt_in(self):
        with mock.patch.dict(os.environ,{'HW_WAV':'0'}):
            with self.assertRaises(ValueError):mod.write_wav(Path(self.tmp.name)/'a.wav',np.zeros((2,2)),22050)

if __name__=='__main__':
    unittest.main()
