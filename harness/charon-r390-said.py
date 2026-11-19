"""r390: print what Charon said in a serial log after a mark (R: reader, M: messages).
  charon-r390-said.py LOG [FROM_COUNT]   -> prints lines, last line 'COUNT n'"""
import re,sys
t=open(sys.argv[1],'rb').read().decode('utf-8','replace')
out=[]
for m in re.finditer(r'(CHARON speak|CHARON message)\r?\n(?:(?:pl|en)\r?\n)?([^\r\n]*)',t):
    if m.group(2).strip() and not m.group(2).startswith('CHARON'):out.append(('M: ' if 'message' in m.group(1) else 'R: ')+m.group(2).replace('\x1b','|'))
start=int(sys.argv[2]) if len(sys.argv)>2 else 0
for x in out[start:]:print('  ',x[:400])
print('COUNT',len(out))
