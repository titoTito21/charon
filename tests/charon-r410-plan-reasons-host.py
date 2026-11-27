"""r410: every fixed reason a plan's step runner or finish step can give has a Polish text.

Reads the sources; nothing is compiled. A reason is a string literal given to why.S(...), to the
lambda fail(...), to plan::MarkFailed(...) or to plan::Trouble(...). Each must stand in the table
of plan::Reason (src/efi/CharonPlanState.h), with a Polish text that is not the English one, has
no English leftovers of the kind seen in r409 and does not end in punctuation (the callers add it).
  python3.15 tests/charon-r410-plan-reasons-host.py        exit code 1 on a failure"""
import re, sys
from pathlib import Path
src = Path(__file__).resolve().parents[1] / 'src/efi'
state = (src / 'CharonPlanState.h').read_text(encoding='utf-8')
LIT = r'"((?:[^"\\]|\\.)*)"'
table = dict(re.findall(r'\{' + LIT + r',\s*' + LIT + r'\},', state[state.index('struct ReasonText'):state.index('inline void Trouble')]))
failed = 0; checks = 0
def check(ok, text):
    global failed, checks
    checks += 1
    if not ok:
        failed += 1; print('FAIL', text)
used = set()
for name in ('CharonWsPlan.h', 'CharonWsPlanDialogue.h', 'CharonWsPlanWindows.h', 'CharonWsWinPart.h', 'CharonPlanState.h'):
    p = src / name
    if not p.exists(): continue
    text = p.read_text(encoding='utf-8')
    found = re.findall(r'\bwhy\.S\(' + LIT + r'\)', text) + re.findall(r'\bfail\(' + LIT + r'\)', text) \
        + re.findall(r'MarkFailed\([^;]*?,\s*' + LIT + r'\s*,', text) + re.findall(r'plan::Trouble\(trouble,\s*' + LIT, text) \
        + [m.replace('\\"', '') for m in re.findall(r'-PlanText \\"([A-Za-z ]+)\\"', text)]
    for literal in found:
        if literal == '': continue
        used.add(literal)
        check(literal in table, f'{name}: no Polish text for the reason "{literal}"')
    check('trouble.S(' not in text, f'{name}: a note of the finish step bypasses plan::Trouble')
check(len(used) >= 45, f'only {len(used)} reasons found in the sources: the patterns of this test no longer match')
for en, pl in table.items():
    check(pl != en and pl.strip() != '', f'the Polish text of "{en}" is missing')
    check(not pl.rstrip().endswith((';', '.', ',')) and not en.rstrip().endswith((';', '.', ',')), f'"{en}": ends in punctuation')
    check(not re.search(r'\b(the|could|not|was|of)\b', pl), f'"{en}": English words in the Polish text')
    check(en in used, f'the table holds a reason no source gives: "{en}"')
print(f'{checks - failed} passed, {failed} failed ({len(used)} reasons in the sources, {len(table)} in the table)')
sys.exit(1 if failed else 0)
