import sys,struct,collections
d=open(sys.argv[1],'rb').read()
lo,hi=0x51020,0xe6800
c=collections.Counter()
for o in range(lo,hi-3,4):
    v,=struct.unpack_from('<I',d,o)
    if 0x01000000<=v<0x08000000 or 0x40000000<=v<0x50000000 and v&0xfff==0 or 0x00000000<v<0x00040000 and False:
        c[v]+=1
print("round/peripheral-like 32-bit values (aligned) in exe:")
for v,n in sorted(c.items()):
    if v<0x40000000 or (v&0xfff)==0: print(f"  {v:#010x} x{n}")
