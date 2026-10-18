import sys,re
d=open(sys.argv[1],'rb').read()
mn=int(sys.argv[2]) if len(sys.argv)>2 else 6
for m in re.finditer(rb'[\x20-\x7e]{%d,}'%mn,d):
    print(f"A {m.start():#08x} {m.group().decode()}")
for m in re.finditer(rb'(?:[\x20-\x7e]\x00){%d,}'%mn,d):
    print(f"W {m.start():#08x} {m.group().decode('utf-16le')}")
