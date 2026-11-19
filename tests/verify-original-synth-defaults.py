"""The platform priority list must not alter original synth selection algorithms."""
import ast
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
before=ast.parse((Path(sys.argv[1])/'synthDriverHandler.py').read_bytes())
after=ast.parse((root/'porting/synthDriverHandler.py').read_bytes())
for name in ('setSynth','findAndSetNextSynth','getSynthInstance'):
    def get(tree):return next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name==name)
    assert ast.dump(get(before))==ast.dump(get(after)),name
bindings=[n for n in after.body if isinstance(n,ast.ImportFrom) and n.module=='synthDefaultsPlatform']
assert len(bindings)==1 and ast.unparse(bindings[0])=='from synthDefaultsPlatform import defaultSynthPriorityList'
print('PASS exact original synth creation/selection/fallback functions; priority candidates supplied by platform')
