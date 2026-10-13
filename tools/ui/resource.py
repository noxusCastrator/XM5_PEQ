"""Wrap the exact original EQ widgets in a native slide_view. No stock widget edits."""
from pathlib import Path
import importlib.util, hashlib, json
HERE=Path(__file__).resolve().parents[1]
LAB=HERE.parents[1]
spec=importlib.util.spec_from_file_location('stock',LAB/'ui-native-lab/export_stock.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
src=LAB.parent/'roundtrip-v2.43/unpacked/rootfs/release/assets/default/raw/ui/playset/equalizer_page.bin'
raw=src.read_bytes(); root,end=m.parse(raw,4)
assert end==len(raw) and m.MAGIC+m.encode(root)==raw
old=root['children']
def node(kind,name,rect,children=(),extra=()):
 return dict(type=kind,rect=rect,props=[['name',name],*extra],children=list(children))
full=[['self_layout','default(x=0,y=0,w=100%,h=100%)']]
host=node('view','peq_host',[0,0,720,720],extra=full)
root['children']=[node('slide_view','xm5_eq_pager',[0,0,720,720],[
 node('view','stock_eq_host',[0,0,720,720],old,extra=full),host],
 [*full,['active','0'],['vertical','false'],['loop','false'],['animating_time','300']])]
out=m.MAGIC+m.encode(root)
for n in old:
 chunk=raw[n['byte_start']:n['byte_end']]
 assert m.encode(n)==chunk and out.count(chunk)==1
dest=HERE/'out/equalizer_page.bin'; dest.parent.mkdir(exist_ok=True); dest.write_bytes(out)
(HERE/'out/resource-manifest.json').write_text(json.dumps(dict(
 source_sha256=hashlib.sha256(raw).hexdigest(),sha256=hashlib.sha256(out).hexdigest(),
 stock_subtrees_unchanged=True,stock_children=len(old),pager='native AWTK slide_view',
 new_bytes=len(out)-len(raw)),indent=2)+'\n')
print('Resource guard passed: both original child subtrees byte-identical')
