import sys; sys.stdout.reconfigure(encoding="utf-8")
import sys
d=open(sys.argv[1],'rb').read()
lo=int(sys.argv[2],0); hi=int(sys.argv[3],0)
i=lo
while i<hi:
    if d[i]==0: i+=1; continue
    j=d.find(b'\0',i)
    s=d[i:j]
    if len(s)>=3:
        print(f"{i:#08x} {s.decode('utf-8','replace')}")
    i=j+1
