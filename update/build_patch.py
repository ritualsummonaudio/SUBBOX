from pathlib import Path
import hashlib,json,struct,math
import pefile

folder=Path(__file__).resolve().parent
baseline=folder.parent/'baseline'/'SUBBOX-ONYX-1.2.12.vst3'
raw=baseline.read_bytes()
assert hashlib.sha256(raw).hexdigest()=='d5965c2517c67c54917aaf3e2b0dce04b598244630981ff680ccd89aba5ed7b6'
p=pefile.PE(data=raw)
mini_raw=(folder/'Hooks.dll').read_bytes();mini=pefile.PE(data=mini_raw)
assert not getattr(mini,'DIRECTORY_ENTRY_IMPORT',[]) and not mini.OPTIONAL_HEADER.DATA_DIRECTORY[5].Size
align=lambda n,a:(n+a-1)//a*a
rva=align(p.OPTIONAL_HEADER.SizeOfImage,4096)
mapped=bytearray(align(mini.OPTIONAL_HEADER.SizeOfImage,4096))
mapped[:mini.OPTIONAL_HEADER.SizeOfHeaders]=mini_raw[:mini.OPTIONAL_HEADER.SizeOfHeaders]
for s in mini.sections:
    mapped[s.VirtualAddress:s.VirtualAddress+s.SizeOfRawData]=mini_raw[s.PointerToRawData:s.PointerToRawData+s.SizeOfRawData]
ex=mini.OPTIONAL_HEADER.DATA_DIRECTORY[0].VirtualAddress
count,functions,names,ordinals=struct.unpack_from('<IIII',mapped,ex+24)
exports={}
for n in range(count):
    ptr=struct.unpack_from('<I',mapped,names+4*n)[0]
    name=bytes(mapped[ptr:mapped.index(0,ptr)]).decode()
    ordinal=struct.unpack_from('<H',mapped,ordinals+2*n)[0]
    exports[name]=struct.unpack_from('<I',mapped,functions+4*ordinal)[0]
for name,target in [('original_path',0x4950),('original_cos',0x718c0),('original_pow',0x72e80),('prepare_jump',0x5025),('meter_prepare_jump',0x4c35),('engine_tick_jump',0x5ee5)]:
    site=exports[name];assert mapped[site]==0xe9
    struct.pack_into('<i',mapped,site+1,target-rva-site-5)
for name,target in [('native_get_module',0x7c238),('native_get_proc',0x7c230)]:
    site=exports[name];assert mapped[site:site+2]==b'\xff\x25'
    struct.pack_into('<i',mapped,site+2,target-rva-site-6)

b=bytearray(raw);ed=p.OPTIONAL_HEADER.DATA_DIRECTORY[3]
es=p.get_section_by_rva(ed.VirtualAddress);eo=p.get_offset_from_rva(ed.VirtualAddress)
md=mini.OPTIONAL_HEADER.DATA_DIRECTORY[3];records=[]
for cursor in range(md.VirtualAddress,md.VirtualAddress+md.Size,12):
    a,z,u=struct.unpack_from('<III',mapped,cursor)
    assert 0<a<z<len(mapped) and mapped[u]&7==1 and mapped[u]>>3==0
    records.append(struct.pack('<III',rva+a,rva+z,rva+u))
end=eo+ed.Size;extra=b''.join(records)
assert end+len(extra)<=es.PointerToRawData+es.SizeOfRawData and not any(b[end:end+len(extra)])
b[end:end+len(extra)]=extra
struct.pack_into('<I',b,ed.get_field_absolute_offset('Size'),ed.Size+len(extra))
struct.pack_into('<I',b,es.get_field_absolute_offset('Misc_VirtualSize'),max(es.Misc_VirtualSize,ed.Size+len(extra)))
header=p.sections[-1].get_file_offset()+40
assert header+40<=p.OPTIONAL_HEADER.SizeOfHeaders and not any(b[header:header+40])
off=align(len(b),512);b+=bytes(off-len(b))+mapped
struct.pack_into('<8sIIIIIIHHI',b,header,b'.rsau1',len(mapped),rva,len(mapped),off,0,0,0,0,0x60000020)
struct.pack_into('<H',b,p.FILE_HEADER.get_field_absolute_offset('NumberOfSections'),len(p.sections)+1)
struct.pack_into('<I',b,p.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfImage'),rva+len(mapped))
struct.pack_into('<I',b,p.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfCode'),p.OPTIONAL_HEADER.SizeOfCode+len(mapped))
changes=[]
def replace(site,code,label,expected=None):
    o=p.get_offset_from_rva(site);old=bytes(b[o:o+len(code)])
    if expected is not None:assert old==expected,(hex(site),old.hex(),expected.hex())
    b[o:o+len(code)]=code
    changes.append(dict(rva=hex(site),file_offset=hex(o),original=old.hex(),patched=code.hex(),label=label))
def branch(site,target,length=5,kind=0xe8,label='',expected=None):
    replace(site,bytes([kind])+struct.pack('<i',target-site-5)+b'\x90'*(length-5),label,expected)

# The latest requested permanent ranges share the original source/filter and
# audition paths. The stable binary remains the host/security/UI baseline.
branch(0x5ee0,rva+exports['engine_tick_entry'],kind=0xe9,label='Capture the original selected-source inputs for bounded BASS processing',expected=bytes.fromhex('488bc45553'))
branch(0x7c6a,rva+exports['source_stub'],11,label='Keep original source/tube audio; move BASS gain to permanent 60-150 Hz lane',expected=bytes.fromhex('f20f58fa488d8f680d0000'))
branch(0x81b2,rva+exports['frame_stub'],10,label='Apply permanent band gains and capture audition metadata; retire unused history')
stores=[(33212,'f2440f11ac3940650300'),(33222,'f20f11b43948650300'),(33231,'f20f11a43950650300'),(33240,'f2440f11bc3958650300'),(33259,'f2440f11a43960650300'),(33269,'f20f11bc3968650300'),(33278,'f20f11ac3970650300'),(33287,'f20f118c3978650300'),(33296,'f20f11843980650300'),(33305,'f20f11943988650300')]
for site,old in stores:
    old=bytes.fromhex(old);replace(site,b'\x90'*len(old),'Retire unused frame-history write',old)
for site in [0x826d,0x8310]:
    branch(site,rva+exports['path_stub'],label='Audition existing native clipped band contributions',expected=b'\xe8'+struct.pack('<i',0x4950-site-5))
branch(0x8d45,rva+exports['capture_stub'],6,label='Capture pre-audition band analysis',expected=bytes.fromhex('f2410f106500'))
branch(0x8d4b,0x925c,7,0xe9,label='Remove SOLO-only filtering; normal band filters remain',expected=bytes.fromhex('488d9730020000'))
branch(0x92d2,rva+exports['retired_rms'],label='Publish consistent band taps; retire duplicate pre-output RMS',expected=b'\xe8'+struct.pack('<i',0x4cf0-0x92d2-5))
for base in [0x12837,0x11b07]:
    for delta,old in [(4,'f20f59fc'),(8,'f2440f59c4'),(22,'f20f59d4'),(26,'f20f59cc'),(30,'f20f59c4')]:
        old=bytes.fromhex(old);replace(base+delta,b'\x90'*len(old),'Keep band metering independent of output-only guard gain',old)
branch(0x5020,rva+exports['prepare_compact'],kind=0xe9,label='Retire duplicate meter and unused history pages during preparation',expected=bytes.fromhex('488bc45553'))
replace(0x5d8c,b'\x90'*5,'Retire duplicate pre-output RMS preparation',b'\xe8'+struct.pack('<i',0x4c30-0x5d8c-5))
branch(0x4c30,rva+exports['meter_prepare_compact'],kind=0xe9,label='Discard unused capacity beyond the exact native 50 ms RMS window',expected=bytes.fromhex('48895c2418'))

# Only the SBO master constants are changed; SUB/BASS ceilings are untouched.
for site,name in [(0x12339,'sbo_stub_a'),(0x11609,'sbo_stub_b')]:
    assert p.get_data(site,4)==bytes.fromhex('f20f1015')
    branch(site,rva+exports[name],8,label='Recover available SBO headroom while retaining reconstruction protection')
limit=.9885530471801758
for o,old,new in [(0x8ea30,.9988493323326111,limit),(0x961f0,.9988493323326111,limit),(0x96810,-.9988493323326111,-limit),(0x96260,2*.9988493323326111,2*limit),(0x961e8,.9988493323326111-1e-5,limit-1e-5)]:
    replace(p.get_rva_from_offset(o),struct.pack('<d',new),'SBO -0.1 dBFS float-safe hard ceiling',struct.pack('<d',old))
for site,old,new in [(0x12799,192,256),(0x11a69,-192,-128)]:
    replace(site,struct.pack('<i',new),'Reduce measured combined guard delay to 384 samples',struct.pack('<i',old))
for site in [0x9b11,0x9b3f,0xa1c7,0xc7d1,0xc966,0xcad6,0xf6f1,0x11389]:
    replace(site,struct.pack('<I',384),'Match native host/PDC/tail/configuration delay',struct.pack('<I',512))
replace(0x8fa88,struct.pack('<d',0.),'SC OFF in native parameter defaults',struct.pack('<d',.5))
replace(0xc243,b'\x31\xc9'+b'\x90'*8,'SC OFF in controller default metadata',bytes.fromhex('48b9000000000000e03f'))
replace(0xc257,bytes.fromhex('660f57c9')+b'\x90'*4,'SC OFF in initial controller value',p.get_data(0xc257,8))

new=pefile.PE(data=b)
struct.pack_into('<I',b,new.OPTIONAL_HEADER.get_field_absolute_offset('CheckSum'),new.generate_checksum())
final=pefile.PE(data=b);assert final.verify_checksum()
assert [e.struct.BeginAddress for e in final.DIRECTORY_ENTRY_EXCEPTION]==sorted(e.struct.BeginAddress for e in final.DIRECTORY_ENTRY_EXCEPTION)
(folder/'Candidate.vst3').write_bytes(b)
identity=dict(label='ONYX stable-binary scoped update U1',baseline_sha256=hashlib.sha256(raw).hexdigest(),module_sha256=hashlib.sha256(b).hexdigest(),bytes=len(b),changes=changes,new_RX_bytes=len(mapped),new_unwind_records=len(records),latency_samples=384,hard_ceiling_dbfs=20*math.log10(limit),soft_entry_dbfs=-.99,SC_default=0.,permanent_ranges_hz={'SUB':[20,60],'BASS':[60,150]},same_processed_SOLO_lanes=True,new_heap_buffers=0,reference_source='User-supplied ONYX 1.2.7; reference and layout model, current binary retained',native_Windows_FL_Studio='NOT RUN',fresh_owner_seal_required=True)
(folder/'Patch-Identity.json').write_text(json.dumps(identity,indent=2)+'\n')
print(json.dumps({k:v for k,v in identity.items() if k!='changes'},indent=2))
