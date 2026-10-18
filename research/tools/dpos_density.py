import sys,struct
d=open(sys.argv[1],'rb').read()
lo=0x51020; hi=0xe6800
print("blk(file off) addr  frac_cond_AL  frac_zero  frac_ascii")
for o in range(lo-0x20,hi,0x1000):
    b=d[o:o+0x1000]; n=len(b)//4
    w=struct.unpack('<%dI'%n,b[:n*4])
    al=sum(1 for x in w if (x>>28)==0xE)/n
    z=sum(1 for x in w if x==0)/n
    a=sum(1 for c in b if 32<=c<127)/len(b)
    print(f"{o:#08x} {0x40000000+o-lo:#010x}  {al:.2f} {z:.2f} {a:.2f}")
