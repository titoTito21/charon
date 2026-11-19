"""Immutable built-in TVox normalization rules and Python Unicode predicates."""
import hashlib,json,unicodedata
from generate_pl import HERE,VOICE,load,literal
original=load('textproc')
def generate():
    lines=['/* Source-derived TVox textproc.py; MIT-0. */']
    for name in ('PL_LETTERS','EN_LETTERS','PL_ABBREV','EN_UNITS'):
        value=getattr(original,name)
        lines.append('static const Pair TP_'+name+'[]={'+','.join('{'+literal(k)+','+literal(v)+'}' for k,v in value.items())+'};')
    for name in ('WORD_ACRONYMS','ALWAYS_SPELL','INTERJECTIONS','_PL_ABBREV_END','_PL_ABBREV_DOT_ONLY','_EN_ONES','_EN_TENS','_ORD','_ORD_TENS'):
        values=getattr(original,name)
        if isinstance(values,set):values=sorted(values)
        lines.append('static const S TP_'+name+'[]={'+','.join(map(literal,values))+'};')
    lines.append('static const Unit PL_UNITS[]={'+','.join('{'+literal(k)+',{'+','.join(map(literal,v))+'}}' for k,v in original.PL_UNITS.items())+'};')
    # Each source pattern is represented as its exact literal stem, optional
    # hyphen and word-boundary requirements, not a replacement pronunciation.
    entries=[]
    for pattern,replacement in original.PL_SPOKEN:
        assert pattern.startswith(r'\b')
        stem=pattern[2:];end=stem.endswith(r'\b')
        if end:stem=stem[:-2]
        if stem.endswith(r'(?=\w*\b)'):stem=stem[:-len(r'(?=\w*\b)')]
        optional='-?' in stem;stem=stem.replace('-?','')
        assert all(c.isalpha() for c in stem)
        entries.append('{'+literal(stem)+','+literal(replacement)+','+str(int(end))+','+str(int(optional))+'}')
    lines.append('static const Spoken PL_SPOKEN[]={'+','.join(entries)+'};')
    predicates={'ALNUM':str.isalnum,'DECIMAL':str.isdecimal,'DIGIT':str.isdigit,'ALPHA':str.isalpha,'UPPER':str.isupper,
        'CASED':lambda c:c.islower() or c.isupper() or c.istitle(),'SPACE':str.isspace,
        'IGNORABLE':lambda c:('A'+c+'Σ').lower().endswith('ς')}
    for name,predicate in predicates.items():
        ranges=[];start=None
        for n in range(0x110001):
            hit=n<0x110000 and predicate(chr(n))
            if hit and start is None:start=n
            if not hit and start is not None:ranges.append((start,n-1));start=None
        lines.append('static const Range UC_'+name+'[]={'+','.join('{%d,%d}'%r for r in ranges)+'};')
    lines.append('static const Digit UC_DIGITS[]={'+','.join('{%d,%d}'%(n,unicodedata.decimal(chr(n))) for n in range(0x110000) if chr(n).isdecimal())+'};')
    (HERE/'tvox_textproc_data.inc').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    (HERE/'TEXTPROC-PROVENANCE.json').write_text(json.dumps({'unicode_version':unicodedata.unidata_version,'textproc_sha256':hashlib.sha256((VOICE/'textproc.py').read_bytes()).hexdigest()},indent=2)+'\n')
if __name__=='__main__':generate()
