from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(__file__).resolve().parents[1];work=root/'update'
digest=hashlib.sha256((work/'Candidate.vst3').read_bytes()).hexdigest()
for name in ['verify_scope.py','verify_bands.py','verify_update.py','verify_guard.py','verify_periodic.py']:
    subprocess.run([sys.executable,str(work/name)],cwd=root,check=True)
reports=[work/'Scope-Proof.json',work/'band-proof/Results.json',work/'update-proof/Results.json',work/'guard-proof-U1/Results.json',work/'periodic-proof/Results.json']
results=[json.loads(p.read_text()) for p in reports]
assert all(x['module_sha256']==digest and x['failures']==0 for x in results)
summary=dict(module_sha256=digest,groups=len(results),cases=sum(x['case_count'] for x in results),failures=0,platform=sys.platform,reports=[str(p.relative_to(root)) for p in reports],native_VST3_DLL_loaded=sys.platform=='win32',full_VST3_host_editor=False,FL_Studio=False,owner_seal_required=True)
(work/'Verification-Summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2),flush=True)
