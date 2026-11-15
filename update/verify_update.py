from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import hashlib,itertools,json,os,re,subprocess
import numpy as np

root=Path(__file__).resolve().parent
module=root/'Candidate.vst3';baseline=root.parent/'baseline/SUBBOX-ONYX-1.2.12.vst3'
digest=hashlib.sha256(module.read_bytes()).hexdigest()
out=root/'update-proof';out.mkdir(exist_ok=True)
rows=[]
def render(name,old=False,clip=0,solo=0,hz=40.,sub=0.,bass=0.,amp=.15,rate=48000,seconds=1.25,settings=None):
    env=os.environ.copy();env.update(SBO_ENGINE_FINAL_GUARD='1',SBO_TEST_POST_METER='1',SBO_EXPECT_DELAY='512' if old else '384')
    env.update({k:str(v) for k,v in (settings or {}).items()})
    path=out/(name+'.f64')
    result=subprocess.run([str(root/('engine-probe.exe' if os.name=='nt' else 'engine-probe')),str(baseline if old else module),*map(str,[hz,sub,bass,clip,solo,amp,seconds]),str(path),str(rate)],env=env,capture_output=True,text=True,timeout=90)
    assert result.returncode==0,(name,result.returncode,result.stderr,result.stdout)
    y=np.fromfile(path,dtype='<f8').reshape(-1,18);path.unlink()
    assert np.isfinite(y[:,:16]).all(),name
    return y,json.loads(result.stdout),result.stderr
def preserve(job):
    idx,clip,hz,sub,bass,control=job
    kw=dict(clip=clip,hz=hz,sub=sub,bass=bass,amp=.08,settings=control)
    a,_,_=render(f'preserve-new-{idx}',**kw);b,_,_=render(f'preserve-old-{idx}',old=True,**kw)
    error=float(abs(a[:-128,:8]-b[128:,:8]).max())
    assert error<2e-13,(idx,'normal audio/band/RMS drift',error,control)
    return dict(test='normal OFF/SB audio, original band taps and native post-output RMS match stable build',clip=clip,hz=hz,sub_db=sub,bass_db=bass,settings=control,max_error=error,delay_compensation=128,passes=True)
# The newly requested permanent crossover intentionally changes the band taps
# and fixed crossover phase. Compare neutral full-mix magnitude separately in
# verify_bands.py; the former-band waveform assertions no longer apply.
print('New range checks are delegated to verify_bands.py',flush=True)


for clip,amp in itertools.product([0,1,2],[.1,3.]):
    control={'SBO_ENGINE_MULTITONE':1,'SBO_ENGINE_PGE':1}
    full,_,_=render(f'full-{clip}-{amp}',clip=clip,amp=amp,sub=6,bass=-6,settings=control)
    for solo in [1,2,3]:
        y,_,_=render(f'solo-{clip}-{amp}-{solo}',clip=clip,solo=solo,amp=amp,sub=6,bass=-6,settings=control)
        error=float(abs(y[:,2:8]-full[:,2:8]).max())
        assert error<3e-12,(clip,amp,solo,'inconsistent solo band/RMS',error)
        if clip<2:
            expected=(y[:,2:4] if solo==1 else y[:,4:6] if solo==2 else y[:,2:4]+y[:,4:6])
            late=int(.2*48000)
            audition_error=float(abs(y[late:,:2]-expected[late:]).max())
            assert audition_error<3e-12,(clip,amp,solo,'solo-only audio processing',audition_error)
        else:audition_error=None
        if clip:
            assert abs(y[:,2:4]).max()<=.25118863582611084+3e-12
            assert abs(y[:,4:6]).max()<=.5011872053146362+3e-12
        rows.append(dict(test='SOLO retains identical processed band taps and RMS; no SOLO-only filter or gain',clip=clip,amp=amp,solo=solo,band_RMS_error=error,audition_error=audition_error,passes=True))
print('SOLO routing and meter comparisons passed',flush=True)

for clip in [0,1,2]:
    a,_,_=render(f'SC-default-{clip}',clip=clip,settings={'SBO_PROBE_NATIVE_DEFAULTS':1,'SBO_PROBE_SC_AVAILABLE':1})
    b,_,_=render(f'SC-off-{clip}',clip=clip,settings={'SBO_ENGINE_SIDECHAIN':0,'SBO_PROBE_SC_AVAILABLE':1})
    assert np.array_equal(a[:,:16],b[:,:16]),('SC default not OFF',clip)
    rows.append(dict(test='native SC default equals explicit OFF despite available sidechain',clip=clip,passes=True))

for rate,clip,solo in [(44100,0,0),(48000,1,1),(48000,2,2),(96000,2,3),(192000,1,0)]:
    control={'SBO_ENGINE_MULTITONE':1,'SBO_ENGINE_PGE':1,'SBO_ENGINE_AUTOMATION':12}
    expected,_,_=render(f'memory-unavailable-{rate}-{clip}',rate=rate,clip=clip,solo=solo,seconds=.8,settings=dict(control,SBO_DISCARD_UNAVAILABLE=1))
    for mode in ['SBO_DISCARD_POISON','SBO_DISCARD_NORMAL']:
        y,_,log=render(f'memory-{mode}-{rate}-{clip}',rate=rate,clip=clip,solo=solo,seconds=.8,settings=dict(control,**{mode:1}))
        assert np.array_equal(y[:,:16],expected[:,:16]),(rate,clip,solo,mode,'discard affected live state')
        match=re.search(r'discard calls=(\d+) eligible_bytes=(\d+)',log);assert match and int(match[1])>=2
        rows.append(dict(test='retired and unused pages do not affect audio or meters when poisoned/discarded',rate=rate,clip=clip,solo=solo,mode=mode,eligible_bytes=int(match[2]),calls=int(match[1]),passes=True))
print('Memory retirement and unused-capacity checks passed',flush=True)

for clip,solo in itertools.product([0,1,2],[1,2,3]):
    ctrl={'SBO_ENGINE_MULTITONE':1,'SBO_ENGINE_PGE':1,'SBO_ENGINE_AUTOMATION':5}
    a,_,_=render(f'automation-full-{clip}-{solo}',clip=clip,sub=-6,bass=6,amp=.5,seconds=1.5,settings=ctrl)
    b,_,_=render(f'automation-solo-{clip}-{solo}',clip=clip,solo=solo,sub=-6,bass=6,amp=.5,seconds=1.5,settings=ctrl)
    error=float(abs(a[:,2:8]-b[:,2:8]).max())
    assert error<3e-12,(clip,solo,'solo automation changed band/RMS',error)
    return_error=float(abs(a[-12000:,:2]-b[-12000:,:2]).max())
    assert return_error<3e-12,(clip,solo,'return from SOLO',return_error)
    rows.append(dict(test='SOLO automation preserves meters and returns to identical full mix',clip=clip,solo=solo,band_RMS_error=error,return_error=return_error,passes=True))

for rate in [44100,48000,88200,96000,192000]:
    y,_,_=render(f'RMS-{rate}',rate=rate,seconds=.8,settings={'SBO_ENGINE_MULTITONE':1})
    length=int(np.floor(rate*.05+.5))
    for start,meter in [(2,6),(4,7)]:
        power=np.mean(y[:,start:start+2]**2,axis=1);s=np.r_[0.,np.cumsum(power)];i=np.arange(len(y))+1
        expected=np.sqrt(np.maximum(0,(s[i]-s[np.maximum(0,i-length)])/length))
        error=float(abs(y[:,meter]-expected).max());assert error<2e-10,(rate,start,error)
        rows.append(dict(test='unaltered exact 50 ms RMS agrees with independent integration',rate=rate,lane=start,window_samples=length,max_error=error,passes=True))

for clip in [0,1,2]:
    for extra in [{'SBO_PROBE_REPREPARE':1},{'SBO_ENGINE_MULTI_INSTANCE':1},{'SBO_PROBE_BASE':'0x230000000'},{'SBO_ENGINE_SILENT_TAIL':1}]:
        y,_,_=render(f'lifecycle-{clip}-{len(rows)}',clip=clip,sub=-3,bass=3,seconds=5. if 'SBO_ENGINE_SILENT_TAIL' in extra else 1.25,settings=dict({'SBO_ENGINE_PGE':1},**extra))
        if 'SBO_ENGINE_SILENT_TAIL' in extra:assert abs(y[-12000:,:8]).max()<1e-22
        rows.append(dict(test='actual DSP lifecycle/reprepare/multi-instance/ASLR/silence',clip=clip,settings=extra,passes=True))

assert hashlib.sha256(module.read_bytes()).hexdigest()==digest
report=dict(module_sha256=digest,case_count=len(rows),failures=0,cases=rows,native_Windows_VST3_host='NOT RUN',FL_Studio='NOT RUN',Windows_RSS='NOT MEASURED; eligible interior pages exercised through discard and poison simulations')
(out/'Results.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='cases'},indent=2),flush=True)
