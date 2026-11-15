"""Package the tested development image, source and honest acceptance status."""
from pathlib import Path
import hashlib,json,subprocess,zipfile

root=Path(__file__).resolve().parents[1]
work=root/'update'
reports=['Scope-Proof.json','band-proof/Results.json','update-proof/Results.json','guard-proof-U1/Results.json','periodic-proof/Results.json','Verification-Summary.json']
image=work/'Candidate.vst3'
digest=hashlib.sha256(image.read_bytes()).hexdigest()
summary=json.loads((work/'Verification-Summary.json').read_text())
assert summary['module_sha256']==digest and summary['failures']==0
for name in reports:
    result=json.loads((work/name).read_text())
    assert result['module_sha256']==digest and result['failures']==0
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
tracked=subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0')
acceptance=dict(source_commit=commit,module_sha256=digest,automated_cases=summary['cases'],native_Windows_DLL_loader=summary['native_VST3_DLL_loaded'],full_VST3_host_editor=False,FL_Studio=False,owner_seal_required=True,latency_samples=384,ranges_hz={'SUB':[20,60],'BASS':[60,150]},SC_default='OFF',SBO_soft_entry_dbfs=-.99,SBO_hard_ceiling_dbfs=-.1)
text=f'''SUB BOX ONYX U1 - OWNER DEVELOPMENT BUILD

SUB 20-60 Hz; BASS 60-150 Hz. SOLO uses the same processed lanes.
SBO soft entry -0.99 dBFS; sample hard ceiling -0.1 dBFS.
SC defaults OFF. Fixed latency is 384 samples (8 ms at 48 kHz).

Module SHA-256: {digest}
Source commit: {commit}
Automated cases in the final verification group: {summary['cases']}; failures: 0.
Native Windows DLL loader tested: {summary['native_VST3_DLL_loaded']}.

The original 1.2.12 production source is unavailable. This package contains
the complete scoped update source and a pinned 1.2.12 production image;
the older 1.2.7 source is retained only as a documented reference.
The original production licensing verifier and public issuer key are retained.
The changed image requires a fresh seal from the existing RSA owner issuer.
No owner private key, credentials or recipient license is included.

This is an unsealed development master. Native FL Studio/editor, listening
and recipient installation/activation acceptance remain pending. The probe
results do not establish those outcomes or a guaranteed Windows RAM saving.
'''
out=root/'handoff';out.mkdir(exist_ok=True)
archive=out/'SUB-BOX-ONYX-U1-Owner-Development.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    z.writestr('README.txt',text)
    z.writestr('ACCEPTANCE.json',json.dumps(acceptance,indent=2)+'\n')
    z.write(image,'BUILD/SUBBOX-ONYX.vst3')
    z.write(work/'Patch-Identity.json','BUILD/Patch-Identity.json')
    for name in reports:z.write(work/name,'QA/final/'+name)
    for p in sorted((root/'qa').rglob('*.json')) if (root/'qa').exists() else []:
        z.write(p,'QA/'+str(p.relative_to(root/'qa')).replace('\\','/'))
    for name in tracked:
        if not name or name.endswith('.zip'):continue
        p=root/name
        if p.is_file():z.write(p,'SOURCE/'+name)
print(json.dumps(dict(file=str(archive),bytes=archive.stat().st_size,sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),acceptance=acceptance),indent=2))
