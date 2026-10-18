import sys,os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),"vendor"))
import sys
from capstone import *
d=open(sys.argv[1],'rb').read()
off=int(sys.argv[2],0); n=int(sys.argv[3],0); base=int(sys.argv[4],0) if len(sys.argv)>4 else off
mode=CS_MODE_ARM
md=Cs(CS_ARCH_ARM,mode)
for i in md.disasm(d[off:off+n],base):
    print(f"{i.address:#010x}  {d[off+i.address-base:off+i.address-base+4].hex()}  {i.mnemonic} {i.op_str}")
