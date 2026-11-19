"""Audit Charon's explicit (QRWA) mode: every explicit wording must really swear.

Collects, straight from the sources, every text Charon can say or show in
explicit mode and checks it against a lexicon of Polish/English swear words
and insults. Prints every text that fails; exit status 1 if any does.

  python tools/audit-charon-explicit.py [--list]

Sources and what is collected:
  src/efi/CharonMenuLabels.h    every variant of every authored option label
  src/efi/CharonMessageMode.h   styled variants of authored messages and hints,
                                interjections, jabs, label tags, idle taunts
  src/efi/CharonResident.h      the reader's explicit phrase arrays
  src/efi/HAKBootEFI.cpp        explicit console lines (SayStyled)
  src/efi/BootMenu.h            explicit console lines and the closing hint
Plain (non-explicit) texts are not audited here; FormatCharonMessage styles
them at run time (tests/CharonMessageModeTests.cpp covers that path).
"""
import re,sys
from pathlib import Path
root=Path(__file__).resolve().parent.parent/'src/efi'
LEX=re.compile(r'kurw|kurew|pieprz|spieprz|wypierd|zjeban|ujeb|chuj|pierd|jeb|pizd|dup[aęyi]|gówn|spierdal|choler|szajs|łamag|ciołk|matol|baran|ćwok|debil|pojeb|frajer|zesra|mord|psiakrew|szlag|skurw|sraj|zasran|gamoń|gamoni|szmat|lamus|ciul|kutas'
               r'|fuck|shit|damn|bloody|arse|dumbass|numpty|dipshit|crap|bollocks|hell|bastard|piss|muppet|bitch|sod |bugger|goddam|wanker|twat|prick|moron|idiot|sucker',re.I)
LIT=re.compile(r'u"((?:[^"\\]|\\.)*)"')
def lits(text):return [m.group(1) for m in LIT.finditer(text)]
found=[]
def add(where,s):
    s=s.strip()
    if s:found.append((where,s))
# 1. option labels: everything inside the ExplicitRow tables
t=(root/'CharonMenuLabels.h').read_text(encoding='utf-8')
body=t[t.index('static const ExplicitRow mainRows'):t.index('#define HAK_ROWS')]
for s in lits(body):add('label',s)
# 2. message mode: entries/hints keep their first literal plain; pools are all explicit
t=(root/'CharonMessageMode.h').read_text(encoding='utf-8')
for line in t.splitlines():
    m=re.match(r'\s*\{u"((?:[^"\\]|\\.)*)",\{(.*)\}\},\s*$',line)
    if m:
        for s in lits(m.group(2)):add('message',s)
for name in ['plError','plSuccess','plInfo','enError','enSuccess','enInfo','pl','en']:
    for m in re.finditer(r'static const char16_t\* '+name+r'\[\]=\{(.*?)\};',t,re.S):
        for s in lits(m.group(1)):add('pool:'+name,s)
# 3. resident reader phrase arrays
t=(root/'CharonResident.h').read_text(encoding='utf-8')
for m in re.finditer(r'static const char16_t\* ((?:pl|en)\w+)\[\]=\{(.*?)\};',t,re.S):
    for s in lits(m.group(2)):add('reader:'+m.group(1),s)
# 4. console lines
t=(root/'HAKBootEFI.cpp').read_text(encoding='utf-8')
for m in re.finditer(r'SayStyled\((u"(?:[^"\\]|\\.)*"),(u"(?:[^"\\]|\\.)*"),(u"(?:[^"\\]|\\.)*")\)',t):
    for s in lits(m.group(2)+m.group(3)):add('console',s.replace('\\r\\n',''))
t=(root/'BootMenu.h').read_text(encoding='utf-8')
for m in re.finditer(r'static const char16_t\* ((?:pl|en)Close)\[\]=\{(.*?)\};',t,re.S):
    for s in lits(m.group(2)):add('menu:'+m.group(1),s)
for m in re.finditer(r'Say\(settings\.explicitMessages\?\(pl\?(u"(?:[^"\\]|\\.)*"):(u"(?:[^"\\]|\\.)*")\)',t):
    for s in lits(m.group(1)+m.group(2)):add('menu console',s.replace('\\r\\n',''))
# 5. explicit boot problem messages (BootMessage(...,true))
t=(root/'BootMessages.h').read_text(encoding='utf-8')
start=t.index('if(explicitMessages) {switch(problem) {')
explicit=t[start:t.index('}}',start)]
for s in lits(explicit):add('boot problem',s)
# 6. language: an English swear tail in a Polish label (and the reverse).
ENSW=[', damn it',', fuck it',', for fuck’s sake',', shit',', you bastard',', bloody hell',', you twat',', dumbass',', you prick',', goddammit',', you muppet',', shithead']
PLSW=[', kurwa mać',', do kurwy nędzy',', ja pierdolę',', w chuj',', jebany świat',', chuj z tym',', do chuja',', w dupę jeża',', jebana jego mać',', łamago',', ciołku',', pojebie',', w pizdu',', kurwa jego mać']
ALLOW={'Po angielsku, fucking English','Polish, kurwa'}  # deliberate jokes about the language itself
mixed=[]
lt=(root/'CharonMenuLabels.h').read_text(encoding='utf-8')
for m in re.finditer(r'\{\{(u".*?)\},\s*\{(u".*?)\}\}',lt,re.S):
    for s in lits(m.group(1)):
        if s not in ALLOW and any(w in s for w in ENSW):mixed.append(('label pl',s))
    for s in lits(m.group(2)):
        if s not in ALLOW and any(w in s for w in PLSW):mixed.append(('label en',s))
rt=(root/'CharonResident.h').read_text(encoding='utf-8')+(root/'CharonMessageMode.h').read_text(encoding='utf-8')
for m in re.finditer(r'static const char16_t\* (pl|en)(\w*)\[\]=\{(.*?)\};',rt,re.S):
    for s in lits(m.group(3)):
        if any(w in s for w in (ENSW if m.group(1)=='pl' else PLSW)):mixed.append(('pool '+m.group(1)+m.group(2),s))
for w,s in mixed:print('WRONG LANGUAGE',w,'|',s)
bad=[(w,s) for w,s in found if not LEX.search(s)]+mixed
if '--list' in sys.argv:
    for w,s in found:print(w,'|',s)
for w,s in bad:print('NOT EXPLICIT',w,'|',s)
print(f'{len(found)} explicit texts, {len(bad)} without a swear word or insult')
sys.exit(1 if bad else 0)
