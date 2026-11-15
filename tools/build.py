from pathlib import Path
import argparse,os,shutil,subprocess,sys

root=Path(__file__).resolve().parents[1];work=root/'update'
parser=argparse.ArgumentParser(description='Build the scoped ONYX patch or its native verification probes.')
parser.add_argument('--probes-only',action='store_true',help='Compile native probes without replacing the already verified VST3 image.')
args=parser.parse_args()
def run(args):
    print(' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),check=True,cwd=root)
windows=os.name=='nt'
if windows:
    cc=shutil.which('clang-cl') or r'C:\Program Files\LLVM\bin\clang-cl.exe'
    assembler=shutil.which('clang') or r'C:\Program Files\LLVM\bin\clang.exe'
    linker=shutil.which('lld-link') or r'C:\Program Files\LLVM\bin\lld-link.exe'
    for name in ([] if args.probes_only else ['hooks','targets']):
        run([cc,'/nologo','/std:c++17','/O2','/GS-','/GR-','/EHs-c-','/Zl','/clang:-ffreestanding','/c',work/(name+'.cpp'),'/Fo'+str(work/(name+'.obj'))])
    for name,out in ([] if args.probes_only else [('hooks.S','hooks-stubs.obj'),('guard-stubs.S','guard-stubs.obj')]):
        run([assembler,'--target=x86_64-pc-windows-msvc','-c',work/name,'-o',work/out])
    if not args.probes_only:
        run([linker,'/dll','/noentry','/nodefaultlib','/machine:x64','/opt:ref','/opt:icf','/align:16','/filealign:16','/timestamp:0','/def:'+str(work/'exports.def'),'/out:'+str(work/'Hooks.dll'),*[work/n for n in ['hooks.obj','hooks-stubs.obj','targets.obj','guard-stubs.obj']]])
    for name in ['engine_probe','guard_probe']:
        run([cc,'/nologo','/std:c++17','/O2','/EHsc','/D_CRT_SECURE_NO_WARNINGS',work/(name+'.cpp'),'/Fe:'+str(work/(name.replace('_','-')+'.exe'))])
else:
    zig=[sys.executable,'-m','ziglang']
    for name in ([] if args.probes_only else ['hooks','targets']):
        run(zig+['c++','-target','x86_64-windows-msvc','-std=c++17','-O2','-fno-exceptions','-fno-rtti','-fno-stack-protector','-ffreestanding','-c',work/(name+'.cpp'),'-o',work/(name+'.obj')])
    for name,out in ([] if args.probes_only else [('hooks.S','hooks-stubs.obj'),('guard-stubs.S','guard-stubs.obj')]):
        run(zig+['cc','-target','x86_64-windows-msvc','-c',work/name,'-o',work/out])
    if not args.probes_only:
        run([shutil.which('x86_64-w64-mingw32-ld') or 'ld','-mi386pep','--dll','--entry=0','--section-alignment=16','--file-alignment=16','--image-base=0x180000000','--no-insert-timestamp','-o',work/'Hooks.dll',*[work/n for n in ['hooks.obj','hooks-stubs.obj','targets.obj','guard-stubs.obj','exports.def']]])
    for name in ['engine_probe','guard_probe']:
        run(['g++','-O2','-std=c++17',work/(name+'.cpp'),'-o',work/name.replace('_','-')])
if not args.probes_only:
    run([sys.executable,work/'build_patch.py'])
