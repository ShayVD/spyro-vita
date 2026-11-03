#!/usr/bin/env python3
"""Compare hardware runs' spyro.log files side by side (decisions D86, D91).

Usage: tools/vita/hw_cmp.py RUN1/spyro.log RUN2/spyro.log ...

Per-vblank means over the windows from vblank 900 on: the `time` lines
(game, ot, wait), the `draw` lines (each renderer, the four converted ones'
sum), the `parse` lines (the walk's busy time, the game thread's wait for
it), the `render` thread's busy time, the atlas's expand, and late vblanks
per 300.
"""
import re
import sys

# per-vblank means over the windows from vblank 900 on: time, draw, parse, render lines; late vblanks
def avg(path, start=900):
    acc={}; n={}
    def add(k,x): acc[k]=acc.get(k,0)+x; n[k]=n.get(k,0)+1
    late=0; wins=0
    for l in open(path,errors='replace'):
        m=re.match(r'\[\s*(\d+)\] vita (draw|parse|time|vblank|render|atlas) (\d+):(.*)',l)
        if not m or int(m.group(3))<start: continue
        kind,rest=m.group(2),m.group(4)
        if kind=='draw':
            tot=0
            for k,x in re.findall(r'(\w+) ([\d.]+)',rest.split('(')[0]):
                if k in ('ms','vblank'): continue
                add(k,float(x))
                if k in ('env','mobys','spyro','sky'): tot+=float(x)
            add('renderers4',tot)
        elif kind=='parse':
            b=re.search(r'busy ([\d.]+) ms',rest); w=re.search(r'waited for the walk ([\d.]+)',rest)
            if b: add('parse busy',float(b.group(1)))
            if w: add('walk wait',float(w.group(1)))
        elif kind=='time':
            for k,x in re.findall(r'(game|ot|wait) ([\d.]+)',rest): add('t.'+k,float(x))
        elif kind=='vblank':
            x=re.search(r'(\d+) vblanks late',rest)
            if x: late+=int(x.group(1)); wins+=1
        elif kind=='render':
            b=re.search(r'busy ([\d.]+) ms per vblank',rest)
            if b: add('render busy',float(b.group(1)))
        elif kind=='atlas':
            x=re.search(r'expand ([\d.]+) ms',rest)
            if x: add('expand/vbl',float(x.group(1))/300)
    r={k:acc[k]/n[k] for k in acc}
    r['late per 300']=late/max(wins,1); r['windows']=wins
    return r
rs=[avg(p) for p in sys.argv[1:]]
keys=['t.game','t.ot','t.wait','update','env','mobys','spyro','sky','renderers4','fx','particles','parse busy','walk wait','render busy','expand/vbl','late per 300','windows']
print('%-14s'%''+''.join('%10s'%p.split('/')[-2][:9] for p in sys.argv[1:]))
for k in keys:
    print('%-14s'%k+''.join('%10.3f'%r.get(k,float('nan')) for r in rs))
