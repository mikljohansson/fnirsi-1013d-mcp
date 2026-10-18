import sys,struct
d=open(sys.argv[1],'rb').read()
STAMP=0x5F0A6C39
def sumw(b): 
    b=b[:len(b)//4*4]; return sum(struct.unpack('<%dI'%(len(b)//4),b))&0xffffffff
for name,o,ln,chkoff in (("BMP",0x1000,0x18a00,12),("EXE",0x51000,0x95800,8)):
    print(name, hex(o), hex(ln))
    for lbl,n in (("len",ln),("len+4k",ln+0x1000),("0x20000",0x20000)):
        blk=d[o:o+n]
        stored=struct.unpack_from('<I',blk,chkoff)[0]
        raw=sumw(blk)
        b=bytearray(blk); struct.pack_into('<I',b,chkoff,STAMP)
        st=sumw(bytes(b))
        b2=bytearray(blk); struct.pack_into('<I',b2,chkoff,0)
        z=sumw(bytes(b2))
        print(f"  {lbl}: stored={stored:#x} raw={raw:#x} stamp={st:#x} zeroed={z:#x}")
    print("  end of image:", hex(o+ln), "bytes after:", d[o+ln:o+ln+16].hex())
