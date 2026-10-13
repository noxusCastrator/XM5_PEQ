"""Guarded demo hook extension. Original init and EQ callbacks stay intact."""
from pathlib import Path
import importlib.util,json,struct,hashlib
HERE=Path(__file__).resolve().parents[1]; WS=HERE.parents[2]
spec=importlib.util.spec_from_file_location('oldpatch',HERE/'scripts/patch.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
stock=(WS/'roundtrip-v2.43/unpacked/rootfs/release/bin/demo').read_bytes()
payload=(HERE/'payload/payload.bin').read_bytes()
size=dict(x.split('=') for x in (HERE/'payload/sizes.txt').read_text().splitlines())
candidate,info=p.make_candidate(stock,payload,int(size['base'],0),len(payload),int(size['memsz']))
symbols={}
for line in (HERE/'payload/payload.map').read_text().splitlines():
 fields=line.split()
 if len(fields)==2 and fields[0].startswith('0x'): symbols[fields[1]]=int(fields[0],16)
target=symbols['u6_send_equalizer']; b=bytearray(candidate)
changes=[(0xe1684,bytes.fromhex('32001c3c4c3e9c27'),struct.pack('<II',0x08000000|(target>>2),0)),
 (0xe43f8,struct.pack('<I',0x10400006),bytes(4)),
 (0xe56b4,struct.pack('<I',0x10400007),bytes(4))]
for off,old,new in changes:
 assert stock[off:off+len(old)]==old,(hex(off),'unexpected baseline')
 b[off:off+len(old)]=new
allowed=set(range(p.HOOK_OFF,p.HOOK_OFF+8))|set(range(info['null_off'],info['null_off']+32))
for off,old,new in changes:allowed.update(range(off,off+len(old)))
assert all(i in allowed for i,(x,y) in enumerate(zip(stock,b)) if x!=y)
assert b[info['app_off']:]==payload
# Re-run the independent U5 verifier after masking just the three new guarded changes.
spec=importlib.util.spec_from_file_location('verify',HERE/'scripts/verify.py')
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
masked=bytearray(b)
for off,old,new in changes:masked[off:off+len(old)]=old
ok,checks=v.verify_candidate(stock,masked,payload,int(size['base'],0),len(payload),int(size['memsz']))
assert ok,checks
(HERE/'out').mkdir(exist_ok=True);(HERE/'out/demo').write_bytes(b)
(HERE/'out/demo-manifest.json').write_text(json.dumps(dict(sha256=hashlib.sha256(b).hexdigest(),
 sender=hex(target),allowed_ranges=[[hex(p.HOOK_OFF),8],[hex(info['null_off']),32]]+
 [[hex(off),len(old)] for off,old,new in changes],base_guard_checks=checks),indent=2)+'\n')
print('U6 demo guards and independent masked U5 verification passed')
