import sys,os,struct,collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),"vendor"))
from capstone import *
from capstone.arm import *
d=open(sys.argv[1],'rb').read()
FILE0=0x51020; BASE=0x40000000
end=int(sys.argv[2],0) if len(sys.argv)>2 else 0xde000
md=Cs(CS_ARCH_ARM,CS_MODE_ARM); md.detail=True
regs={}
consts=collections.Counter()
lit=collections.Counter()
code=d[FILE0:end]
def gen():
    for i in range(0,len(code)-3,4):
        for ins in md.disasm(code[i:i+4],BASE+i): yield ins
for ins in gen():
    if ins.id==ARM_INS_MOVW and ins.operands[1].type==ARM_OP_IMM:
        regs[ins.operands[0].reg]=ins.operands[1].imm&0xffff
    elif ins.id==ARM_INS_MOVT and ins.operands[0].reg in regs:
        v=regs[ins.operands[0].reg]|(ins.operands[1].imm<<16); consts[v]+=1
    elif ins.id==ARM_INS_LDR and ins.op_str.find('[pc,')>=0:
        try:
            imm=int(ins.op_str.split('#')[1].rstrip(']'),0)
            a=ins.address+8+imm; v,=struct.unpack_from('<I',code,a-BASE); lit[v]+=1
        except Exception: pass
allv=collections.Counter(); allv.update(consts); allv.update(lit)
bases=collections.defaultdict(list)
for v,n in allv.items():
    if 0x01c00000<=v<0x01f00000 or 0x02000000<=v<0x08000000 or 0x3f000000<=v<0x40000000 or v<0x00040000 and v>=0x0:
        if v>=0x00040000 or (v&3)==0 and v>0x1000: bases[v&~0xfff].append((v,n))
for b in sorted(bases):
    if b>=0x00040000 or True:
        print(f"{b:#010x}: "+", ".join(f"{v&0xfff:#x}" for v,n in sorted(bases[b]))[:200])
