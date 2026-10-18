import sys,struct,zlib
d=open(sys.argv[1],'rb').read()
start=int(sys.argv[2],0); end=int(sys.argv[3],0)
buf=d[start:end]
px=struct.unpack('<%dH'%(len(buf)//2),buf[:len(buf)//2*2])
def score(w):
    n=len(px)//w
    if n<4: return 1e9
    s=0;c=0
    for r in range(0,n-1):
        a=px[r*w:(r+1)*w]; b=px[(r+1)*w:(r+2)*w]
        s+=sum(1 for x,y in zip(a,b) if x!=y); c+=w
    return s/c
if len(sys.argv)<6:
    res=sorted((score(w),w) for w in range(60,900))[:8]
    print("best widths:",res)
else:
    w=int(sys.argv[4]); out=sys.argv[5]
    h=len(px)//w
    raw=bytearray()
    for r in range(h):
        raw.append(0)
        for x in px[r*w:(r+1)*w]:
            raw+=bytes(((x>>11)*255//31,((x>>5)&63)*255//63,(x&31)*255//31))
    def ch(t,b): 
        c=struct.pack('>I',len(b))+t+b; return c+struct.pack('>I',zlib.crc32(t+b)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+ch(b'IDAT',zlib.compress(bytes(raw)))+ch(b'IEND',b'')
    open(out,'wb').write(png); print("wrote",out,w,h)
