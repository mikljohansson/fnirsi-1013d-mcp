import sys,hashlib,zlib,itertools
d=open(sys.argv[1],'rb').read()
tr=d[0xe7830:]
print("trailer",len(tr),tr.hex())
cands={}
starts=[0,0x1000,0x51000,0x51020]
ends=[0xe6800,0xe7800,0xe7830,0xe7832,0xe7834,len(d)]
tails={bytes(tr[k:k+n]) for k in range(0,10) for n in (4,16,20,32,34)}
found=False
for s in starts:
  for e in ends:
    blk=d[s:e]
    for hn in ('md5','sha1','sha256','sha512'):
        h=hashlib.new(hn,blk).digest()
        for t in tails:
            if len(t)>=4 and (t in h or h.startswith(t)): print("MATCH",hn,hex(s),hex(e),t.hex()); found=True
    c=zlib.crc32(blk)&0xffffffff
    for t in tails:
        if len(t)==4 and (int.from_bytes(t,'little')==c or int.from_bytes(t,'big')==c): print("CRC32 match",hex(s),hex(e)); found=True
print("any match:",found)
