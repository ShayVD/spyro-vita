# Totals of a tools/oracle/sweep_state.sh run (OUTDIR, default logs/run4-sweep): runs, pass, fails; fails classified by whether
# the sample has no sound table (g_Spu.m_SoundTable == 0).
import glob,gzip,struct,re,os,collections,sys
OUT=sys.argv[1] if len(sys.argv)>1 else "logs/run4-sweep"
def soundtab(cap,fn,k):
    for ext in (".in",".in.gz"):
        f=f"logs/caps/{cap}/{fn}_{k}{ext}"
        if os.path.exists(f):
            d=(gzip.open if ext.endswith("gz") else open)(f,"rb").read()
            base=(5+34+32+32)*4+1024
            # g_Spu at 0x80078AE? look up from map
            return struct.unpack_from("<I",d,base+SPU+0x2CC)[0]
for l in open("build/s1-matching/build/psx.map"):
    m=re.match(r"\s+0x([0-9a-f]+)\s+g_Spu\s*$",l)
    if m: SPU=(int(m.group(1),16)&0xffffffff)-0x80000000
tot=collections.Counter(); bad=[]; crashref=collections.Counter()
for f in sorted(glob.glob(OUT+"/*.txt")):
    name,cap=os.path.basename(f)[:-4].split("_",1)
    fn="func_8003EA68" if name.startswith("ea") else "func_8003FE40"
    for l in open(f):
        m=re.match(r"(func_\w+) (\d+) (\d+) (PASS|FAIL|CRASH-REF|CRASH)",l)
        if not m: continue
        st=m.group(4)
        if st=="CRASH-REF": crashref[fn]+=1; continue
        tot[(fn,"runs")]+=1
        if st=="PASS": tot[(fn,"pass")]+=1; continue
        st0=soundtab(cap,fn,m.group(2))
        bad.append((fn,name,cap,l.strip(),st0))
print(dict(tot)); print("crash-ref (not compared):",dict(crashref))
c=collections.Counter((b[0], "nosound" if b[4]==0 else "OTHER") for b in bad); print(dict(c))
for b in bad:
    if b[4]!=0: print(b)
