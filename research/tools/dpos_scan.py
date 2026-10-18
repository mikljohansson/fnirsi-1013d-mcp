import sys, struct, math, zlib, collections
p = sys.argv[1]
d = open(p,'rb').read()
print("size", len(d), hex(len(d)))
# nonzero regions
def regions(d, blk=0x100):
    out=[];start=None
    for i in range(0,len(d),blk):
        nz = any(d[i:i+blk])
        if nz and start is None: start=i
        if not nz and start is not None: out.append((start,i)); start=None
    if start is not None: out.append((start,len(d)))
    return out
print("nonzero regions (256B granularity):")
for a,b in regions(d): print(f"  {a:#x}-{b:#x} len {b-a:#x}")
# entropy per 0x1000
def ent(b):
    if not b: return 0
    c=collections.Counter(b); n=len(b)
    return -sum(v/n*math.log2(v/n) for v in c.values())
print("entropy per 4K block:")
row=[]
for i in range(0,len(d),0x1000):
    e=ent(d[i:i+0x1000]); row.append(f"{i:#07x}:{e:.1f}")
for i in range(0,len(row),8): print("  "+"  ".join(row[i:i+8]))
# eGON header
o=0x1000
j,=struct.unpack_from('<I',d,o)
print("jump",hex(j), "-> branch offset words", j&0xffffff, "target", hex(o+8+((j&0xffffff)<<2)))
print("magic", d[o+4:o+12])
chk,length=struct.unpack_from('<II',d,o+12)
print("checksum",hex(chk),"length",hex(length),length)
# verify checksum: stamp 0x5F0A6C39 in place, sum words over length
b=bytearray(d[o:o+length]); struct.pack_into('<I',b,12,0x5F0A6C39)
s=sum(struct.unpack('<%dI'%(len(b)//4),bytes(b[:len(b)//4*4])))&0xffffffff
print("computed boot0 checksum", hex(s), "OK" if s==chk else "MISMATCH")
print("header words after length:", [hex(x) for x in struct.unpack_from('<12I',d,o+20)])
# magic scan
magics={b'\x1f\x8b\x08':'gzip',b'\x78\x9c':'zlib',b'\x78\x01':'zlib-lo',b'\x78\xda':'zlib-best',b'\x5d\x00\x00':'lzma',b'\x04\x22\x4d\x18':'lz4',
 b'BM':'bmp',b'\x89PNG':'png',b'\xff\xd8\xff':'jpg',b'hsqs':'squashfs',b'\x28\xb5\x2f\xfd':'zstd',b'\xfd7zXZ':'xz',b'eGON':'eGON',b'sunxi':'sunxi',b'ANDROID!':'android',
 b'\xd0\x0d\xfe\xed':'dtb',b'\x55\xaa':'55aa',b'UBI#':'ubi',b'\x85\x19\x01\xe0':'jffs2',b'FAT':'fat',b'NTFS':'ntfs',b'SPL':'spl',b'\x7fELF':'elf',b'PK\x03\x04':'zip'}
print("magic hits:")
for m,n in magics.items():
    if len(m)<=2: continue
    i=-1;c=0
    while True:
        i=d.find(m,i+1)
        if i<0 or c>20: break
        print(f"  {n} at {i:#x}");c+=1
# BM candidates with sane header
i=-1
while True:
    i=d.find(b'BM',i+1)
    if i<0: break
    if i+14<=len(d):
        sz,r,off=struct.unpack_from('<IIH'[:0]+'<IHH',d,i+2)[0],0,0
        sz=struct.unpack_from('<I',d,i+2)[0]; off=struct.unpack_from('<I',d,i+10)[0]
        if 0<sz<=len(d)-i+16 and 14<=off<sz and struct.unpack_from('<I',d,i+14)[0] in (12,40,56,108,124):
            w,h,pl,bpp=struct.unpack_from('<iiHH',d,i+18)
            print(f"  BMP candidate {i:#x} size {sz} off {off} {w}x{h} bpp {bpp}")
# try zlib at every byte starting with 78
for i in range(len(d)-2):
    if d[i]==0x78 and d[i+1] in (0x01,0x5e,0x9c,0xda):
        try:
            o2=zlib.decompressobj().decompress(d[i:i+0x4000]); 
            if len(o2)>64: print(f"  zlib stream at {i:#x} -> >= {len(o2)} bytes")
        except Exception: pass
