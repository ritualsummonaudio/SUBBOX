from pathlib import Path
from concurrent.futures import ThreadPoolExecutor,as_completed
import hashlib,itertools,json,os,subprocess
import numpy as np
from scipy.signal import butter,sosfreqz

root=Path(__file__).resolve().parent;module=root/'Candidate.vst3';digest=hashlib.sha256(module.read_bytes()).hexdigest();out=root/'band-proof';out.mkdir(exist_ok=True)
exe=root/('engine-probe.exe' if os.name=='nt' else 'engine-probe')
def response(rate,hz,solo=0,sub=0,bass=0):
    env=dict(os.environ,SBO_EXPECT_DELAY='384',SBO_ENGINE_FINAL_GUARD='1',SBO_TEST_POST_METER='1',SBO_DISCARD_UNAVAILABLE='1')
    r=subprocess.run([str(exe),str(module),*map(str,[hz,sub,bass,0,solo,.00001,.8]),os.devnull,str(rate)],capture_output=True,text=True,env=env,timeout=60)
    assert r.returncode==0,(rate,hz,solo,r.stderr)
    return json.loads(r.stdout)
def lp(rate,hz,corner,high=False):
    sos=butter(2,corner,btype='highpass' if high else 'lowpass',fs=rate,output='sos')
    return sosfreqz(sos,worN=[hz],fs=rate)[1][0]**2
jobs=list(itertools.product([44100,48000,88200,96000,192000],[10,20,30,40,60,90,120,150,250,1000],[0,1,2]))
def one(job):
    rate,hz,solo=job;r=response(rate,hz,solo)
    if solo==1:expected=abs(lp(rate,hz,150)*lp(rate,hz,60)*lp(rate,hz,20,True))
    elif solo==2:expected=abs(lp(rate,hz,150)*lp(rate,hz,60,True))
    else:expected=1.
    expected_db=20*np.log10(expected);error=abs(r['tone_gain_db']-expected_db)
    # At 10 Hz the 0.8 s fit still contains a small low-frequency tail.
    assert error<(.04 if hz==10 else .006),(job,'wrong permanent crossover response',r['tone_gain_db'],expected_db,error)
    return dict(test='actual control/SOLO tap matches permanent 20/60/150 Hz LR4 crossover',rate=rate,hz=hz,solo=solo,measured_db=r['tone_gain_db'],expected_db=float(expected_db),error_db=float(error),passes=True)
rows=[]
with ThreadPoolExecutor(max_workers=3) as pool:
    futures=[pool.submit(one,job) for job in jobs]
    try:
        for i,future in enumerate(as_completed(futures),1):
            rows.append(future.result())
            if i%10==0:print('Permanent band cases',i,'of',len(jobs),flush=True)
    except BaseException:
        for future in futures:future.cancel()
        raise
rows.sort(key=lambda row:(row['rate'],row['hz'],row['solo']))
print('Permanent band response cases passed',len(rows),flush=True)
for solo,hz in [(1,40),(2,90)]:
    unity=response(48000,hz,solo)['tone_gain_db']
    for gain in [-30,-12,-6,6,12,30]:
        r=response(48000,hz,solo,sub=gain if solo==1 else 0,bass=gain if solo==2 else 0)
        actual=r['tone_gain_db']-unity;error=abs(actual-gain)
        assert error<.035,(solo,hz,gain,actual,error)
        rows.append(dict(test='signed gain belongs to actual selected lane',solo=solo,hz=hz,requested_db=gain,actual_db=actual,error_db=error,passes=True))
assert hashlib.sha256(module.read_bytes()).hexdigest()==digest
report=dict(module_sha256=digest,case_count=len(rows),failures=0,nominal_crossover_hz=[20,60,150],filter='fourth-order Linkwitz-Riley; finite slopes',neutral_magnitude='unity; fixed coherent crossover phase',cases=rows)
(out/'Results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='cases'},indent=2),flush=True)
