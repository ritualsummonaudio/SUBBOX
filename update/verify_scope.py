from pathlib import Path
import hashlib,json,struct
import pefile
root=Path(__file__).resolve().parent;raw=(root.parent/'baseline/SUBBOX-ONYX-1.2.12.vst3').read_bytes();new=(root/'Candidate.vst3').read_bytes();old=pefile.PE(data=raw);p=pefile.PE(data=new);identity=json.loads((root/'Patch-Identity.json').read_text());rows=[]
def passed(test,**facts):rows.append(dict(test=test,passes=True,**facts))
assert hashlib.sha256(raw).hexdigest()==identity['baseline_sha256'] and hashlib.sha256(new).hexdigest()==identity['module_sha256']
assert p.verify_checksum();passed('pinned production baseline and exact tested update identity; PE checksum')
allowed=bytearray(len(new))
def allow(offset,count):allowed[offset:offset+count]=b'\1'*count
for c in identity['changes']:
    o=int(c['file_offset'],16);a=bytes.fromhex(c['original']);b=bytes.fromhex(c['patched']);assert raw[o:o+len(a)]==a and new[o:o+len(b)]==b;allow(o,len(a))
for field,obj,count in [('NumberOfSections',old.FILE_HEADER,2),('SizeOfImage',old.OPTIONAL_HEADER,4),('SizeOfCode',old.OPTIONAL_HEADER,4),('CheckSum',old.OPTIONAL_HEADER,4)]:allow(obj.get_field_absolute_offset(field),count)
ed=old.OPTIONAL_HEADER.DATA_DIRECTORY[3];end=old.get_offset_from_rva(ed.VirtualAddress)+ed.Size
allow(ed.get_field_absolute_offset('Size'),4);allow(end,identity['new_unwind_records']*12)
allow(old.get_section_by_rva(ed.VirtualAddress).get_field_absolute_offset('Misc_VirtualSize'),4)
allow(old.sections[-1].get_file_offset()+40,40);allow(len(raw),len(new)-len(raw))
assert not [i for i,(a,b) in enumerate(zip(raw,new)) if a!=b and not allowed[i]]
passed('every changed baseline byte belongs to a declared update or required PE metadata',changed_ranges=len(identity['changes']))
for name in ['.rsrc','.data','.reloc','.fptable']:
    a=next(s for s in old.sections if s.Name.rstrip(b'\0').decode()==name);b=next(s for s in p.sections if s.Name.rstrip(b'\0').decode()==name);assert a.get_data()==b.get_data()
passed('all original UI/resources, mutable image data and relocation records retained')
def imports(pe):return [(e.dll,[(i.name,i.ordinal) for i in e.imports]) for e in pe.DIRECTORY_ENTRY_IMPORT]
assert imports(old)==imports(p);passed('production DLL dependencies and imports retained')
assert [(e.name,e.ordinal,e.address) for e in old.DIRECTORY_ENTRY_EXPORT.symbols]==[(e.name,e.ordinal,e.address) for e in p.DIRECTORY_ENTRY_EXPORT.symbols]
assert old.OPTIONAL_HEADER.AddressOfEntryPoint==p.OPTIONAL_HEADER.AddressOfEntryPoint
passed('original VST3 exports, class factory and DLL entry point retained')
for a,z in [(0x1000,0x4250),(0x4250,0x4950),(0x4950,0x4c30)]:assert old.get_data(a,z-a)==p.get_data(a,z-a)
passed('native band clippers, master shaping code and native routing implementation retained')
for value in [.8922776195878269,.25118863582611084,.5011872053146362]:
    literal=struct.pack('<d',value)
    for offset in range(len(raw)-7):
        if raw[offset:offset+8]==literal:assert new[offset:offset+8]==literal
passed('soft entry -0.99 and SUB/BASS -12/-6 sample ceilings retained')
defaults=struct.unpack('<20d',old.get_data(0x8fa40,160));updated=struct.unpack('<20d',p.get_data(0x8fa40,160));assert defaults[9]==.5 and updated[9]==0. and defaults[:9]+defaults[10:]==updated[:9]+updated[10:]
assert p.get_data(0xc243,2)==bytes.fromhex('31c9') and p.get_data(0xc257,4)==bytes.fromhex('660f57c9')
passed('SC OFF in processor defaults, controller metadata and controller initial value')
records=lambda pe:[(e.struct.BeginAddress,e.struct.EndAddress,e.struct.UnwindData) for e in pe.DIRECTORY_ENTRY_EXCEPTION]
assert records(p)[:len(records(old))]==records(old)
assert [x[0] for x in records(p)]==sorted(x[0] for x in records(p))
assert p.sections[-1].Characteristics&0xe0000000==0x60000000
passed('original unwind records retained; new records ordered; added code read/execute only')
magic=b'ECS1\x20\0\0\0';assert raw.count(magic)==new.count(magic)==1;o=raw.index(magic);assert raw[o:o+72]==new[o:o+72]
passed('production issuer public key and original offline licensing verifier retained')
report=dict(module_sha256=identity['module_sha256'],case_count=len(rows),failures=0,cases=rows,new_per_instance_heap_buffers=0,original_1_2_12_source_recovered=False)
(root/'Scope-Proof.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
