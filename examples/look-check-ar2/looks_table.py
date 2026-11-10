#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""look-check: the table in mods/core-ar2/knob-looks-1.74.md, from the UI record dump (gen_looks.py) and the
decoded stock 1.74 main OS image (elekloader.formats.main_image). python3 looks_table.py DUMP.bin MAIN.bin"""
import struct,collections,sys
dump,img=sys.argv[1],sys.argv[2]
d=open(dump,'rb').read(); B=0x419a4000; R=0x419a4888
b=open(img,'rb').read(); V=0x40000400; P=0x401bfbc8
def s(a):
    if not a or not (V<=a<V+len(b)): return ''
    o=a-V; e=b.index(b'\0',o); return b[o:e].decode('latin1')
def prm(i):
    o=P+52*i-V; r=struct.unpack('>13I',b[o:o+52]); return r
recs=[struct.unpack('>21I',d[R-B+84*i:R-B+84*i+84]) for i in range(469)]
def f(r,o): k=o//4; return (r[k+2],r[k+3])
def key(r): return (r[0],)+r[1:5]+tuple(f(r,o) for o in (0x14,0x24,0x34,0x44))
g=collections.OrderedDict()
for i,r in enumerate(recs): g.setdefault(key(r),[]).append(i)
grs=collections.OrderedDict()
for i,r in enumerate(recs): grs.setdefault(f(r,0x24),[]).append(i)
gnum={k:n+1 for n,k in enumerate(grs)}
def name(i):
    p=prm(i); return s(p[10]), s(p[12])
def rng(i):
    p=prm(i); mn,mx=[v-(1<<32) if v>>31 else v for v in p[2:4]]
    fmt=lambda v: ('%d'%(v>>8)) if v%256==0 else '%.2f'%(v/256)
    return '%s..%s'%(fmt(mn),fmt(mx))
out=[]
out.append('| look | sample id | its name (label) | range | graphic | flags | pop-up | ids with the same UI record |')
out.append('|---|---|---|---|---|---|---|---|')
n=0
for k,ids in g.items():
    rep=ids[0] if ids[0] else (ids[1] if len(ids)>1 else 0)
    n+=1
    r=recs[rep]; nm,lb=name(rep)
    pop='yes' if f(r,0x44)!=(0,0) else 'no'
    out.append('| %d | %d | %s (%s) | %s | G%d | %05x | %s | %s |'%(n,rep,nm or '-',lb or '-',rng(rep),gnum[f(r,0x24)],r[0],pop,' '.join(map(str,ids))))
print('\n'.join(out))
