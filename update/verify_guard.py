import hashlib as _pin_hashlib
from pathlib import Path as _PinPath
_PIN_MODULE=_PinPath(__file__).resolve().parent/"Candidate.vst3"
_PIN_SHA=_pin_hashlib.sha256(_PIN_MODULE.read_bytes()).hexdigest()
from pathlib import Path
import concurrent.futures,subprocess,json,math,hashlib,re,time,zipfile
import os
import numpy as np
from scipy.signal import firwin,resample_poly
root=Path(__file__).resolve().parent;plugin=root/'Candidate.vst3';exe=root/('guard-probe.exe' if os.name=='nt' else 'guard-probe');out=root/'guard-proof-U1';out.mkdir(exist_ok=True)
kernels=[(f'16x-span{span}-kaiser{beta}',16,firwin(span*16+1,1/16.,window=('kaiser',beta))) for span in [16,32,64,128] for beta in [5.,9.,12.]]
kernels.append(('32x-span256-kaiser12',32,firwin(256*32+1,1/32.,window=('kaiser',12.))))
jobs=[(rate,m,340) for rate in [44100,48000,88200,96000,192000] for m in range(16)]
jobs += [(48000,2,seed) for seed in range(64)] + [(48000,10,seed) for seed in range(24)]
jobs += [(rate,m,340) for rate in [44100,48000,88200,96000,192000] for m in [16,17]]
jobs += [(rate,m,340) for rate in [44100,48000,88200,96000,192000] for m in [20,21,22,23,24]]
def one(args):
 rate,mode,seed=args;name=f'{rate}-{mode:02}-{seed:03}';path=out/(name+'.f64');p=subprocess.run([str(exe),str(plugin),str(mode),str(path),'file',str(rate),str(seed)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120 if os.name=='nt' else 30)
 assert p.returncode==0,(name,p.stdout)
 raw=path.read_bytes();x=np.frombuffer(raw,dtype='<f8').reshape(-1,2);assert np.isfinite(x).all();peak=float(20*np.log10(np.abs(x).max()));row={'case':name,'rate':rate,'mode':mode,'seed':seed,'sample_dbfs':peak,'sha256':hashlib.sha256(raw).hexdigest()}
 if mode in [16,17]:row['exact_384_delay_and_metadata']=True;return row
 assert peak<=-.10,(name,'sample',peak)
 readers=[]
 for label,factor,k in kernels:
  tp=float(20*np.log10(np.abs(resample_poly(x,factor,1,axis=0,window=k)).max()));readers.append({'reader':label,'dbtp':tp})
 # f32 host quantization is also measured; the plugin's final guard precedes the host sample conversion.
 if rate==48000:
  y=x.astype(np.float32).astype(np.float64);k=kernels[-1];tp=float(20*np.log10(np.abs(resample_poly(y,k[1],1,axis=0,window=k[2])).max()));readers.append({'reader':'f32-output-32x256','dbtp':tp});assert np.abs(y).max()<=np.float32(.9885530471801758)
 p=subprocess.run(['ffmpeg','-hide_banner','-nostats','-f','f64le','-ar',str(rate),'-ac','2','-i',str(path),'-af','ebur128=peak=true:metadata=1,ametadata=mode=print:file=-','-f','null','-'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120 if os.name=='nt' else 30)
 assert p.returncode==0,p.stdout[-500:];values=re.findall(r'lavfi.r128.true_peak(?:s_ch\d+)?=([0-9.eE+-]+)',p.stdout);assert values,name
 tp=20*math.log10(max(map(float,values)));readers.append({'reader':'FFmpeg-ebur128','dbtp':tp})
 row['readers']=readers;row['worst_dbtp']=max(r['dbtp'] for r in readers);row['passes_ceiling']=row['worst_dbtp']<=-.10;return row

rows=[];begin=time.time()
with concurrent.futures.ThreadPoolExecutor(max_workers=2 if os.name=='nt' else 4) as pool:
 for row in pool.map(one,jobs):
  rows.append(row)
  if len(rows)%16==0:print('Guard cases',len(rows),'of',len(jobs),round(time.time()-begin,1),flush=True)
worst=max((r for r in rows if 'worst_dbtp' in r),key=lambda r:r['worst_dbtp'])
failures=[r for r in rows if not r.get('passes_ceiling',True)]
report=dict(module_sha256=hashlib.sha256(plugin.read_bytes()).hexdigest(),case_count=len(rows),meter_checks=sum(len(r.get('readers',[])) for r in rows),failures=len(failures),worst_dbtp=worst['worst_dbtp'],worst_case=worst['case'],latency_samples=384,cases=rows,Windows_DAW='NOT RUN',FL_Studio='NOT RUN')
(out/'Results.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='cases'},indent=2),flush=True)
assert not failures,[(r['case'],r['worst_dbtp']) for r in failures]

assert _pin_hashlib.sha256(_PIN_MODULE.read_bytes()).hexdigest()==_PIN_SHA,"Module changed during tests"
