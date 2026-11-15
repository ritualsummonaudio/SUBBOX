import hashlib as _pin_hashlib
from pathlib import Path as _PinPath
_PIN_MODULE=_PinPath(__file__).resolve().parent/"Candidate.vst3"
_PIN_SHA=_pin_hashlib.sha256(_PIN_MODULE.read_bytes()).hexdigest()
from pathlib import Path
import concurrent.futures, hashlib, json, math, re, subprocess
import os
import numpy as np
from scipy.signal import firwin, resample_poly

root = Path(__file__).resolve().parent
module = root / 'Candidate.vst3'
exe = root / ('guard-probe.exe' if os.name=='nt' else 'guard-probe')
out = root / 'periodic-proof'
out.mkdir(exist_ok=True)
kernel = firwin(256 * 32 + 1, 1 / 32., window=('kaiser', 12.))
jobs = [(mode, period, seed) for mode in [40, 41]
        for period in [2, 3, 4, 7, 16, 31, 48, 64, 127, 128]
        for seed in [11, 340]]
jobs += [(mode, period, 340) for mode in [42, 43]
         for period in [16, 31, 48, 64, 127, 128]]

def one(job):
    mode, period, seed = job
    name = f'mode{mode}-period{period}-seed{seed}'
    p = out / (name + '.f64')
    r = subprocess.run([str(exe), str(module), str(mode), str(p), 'file',
                        '48000', str(seed), str(period)], capture_output=True,
                       text=True, timeout=30)
    assert r.returncode == 0, (name, r.stdout, r.stderr)
    x = np.fromfile(p, dtype='<f8').reshape(-1, 2)
    assert np.isfinite(x).all()
    sample = float(20 * np.log10(np.abs(x).max()))
    tp = float(20 * np.log10(np.abs(resample_poly(x, 32, 1, axis=0, window=kernel)).max()))
    r = subprocess.run(['ffmpeg', '-hide_banner', '-nostats', '-f', 'f64le',
                        '-ar', '48000', '-ac', '2', '-i', str(p), '-af',
                        'ebur128=peak=true:metadata=1,ametadata=mode=print:file=-',
                        '-f', 'null', '-'], capture_output=True, text=True, timeout=30)
    assert r.returncode == 0, r.stderr
    values = re.findall(r'lavfi.r128.true_peak(?:s_ch\d+)?=([0-9.eE+-]+)', r.stdout)
    assert values, name
    fft = 20 * math.log10(max(map(float, values)))
    return dict(case=name, sample_dbfs=sample, scipy_dbtp=tp,
                ffmpeg_dbtp=fft, passes=max(sample, tp, fft) <= -.10)

rows = []
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    for row in pool.map(one, jobs):
        rows.append(row)
        print(json.dumps(row), flush=True)
report = dict(module_sha256=hashlib.sha256(module.read_bytes()).hexdigest(),
              case_count=len(rows),cases=rows, failures=sum(not row['passes'] for row in rows))
(out / 'Results.json').write_text(json.dumps(report, indent=2) + '\n')
assert report['failures'] == 0

assert _pin_hashlib.sha256(_PIN_MODULE.read_bytes()).hexdigest()==_PIN_SHA,"Module changed during tests"
