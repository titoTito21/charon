"""r410: character and word error rate of the reader's screen text against hand-written references.

  python charon-ocr-measure.py REFERENCE.txt GRID_ROOT [--lines]

REFERENCE.txt: tests/charon-screens/ocr-reference.txt ("== set/screen" blocks, one screen line per line).
GRID_ROOT: a folder with <set>/<screen>.grid files written by a HAK_CHARON_GRID_DUMP build
(tests/charon-screens, or a fresh capture such as work/r410-run2).

For every reference line the best place in the reader's grid is found (the line may share a grid row with
other text, so the distance is the edit distance to the best-matching part of any row; spaces are
collapsed). Character error rate = sum of those distances / reference characters. A word counts as right
when it stands, unchanged, in the row that matched best. A reference line with more than half of its
characters wrong is also counted as a MISSED line. Nothing here corrects or guesses.
"""
import sys, re
from pathlib import Path


def squash(s):
    return re.sub(r'\s+', ' ', s.strip())


def grid_rows(path):
    rows = []
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        if line.startswith('G') and '|' in line[:400]:
            parts = line.split('|', 2)
            if len(parts) == 3:
                t = squash(re.sub(r'[─-╿]', ' ', parts[2]))
                if t:
                    rows.append(t)
    return rows


def infix_distance(ref, row):
    """Edit distance between ref and the best-matching substring of row."""
    n = len(ref)
    prev = [0] * (len(row) + 1)
    for i in range(1, n + 1):
        cur = [i] + [0] * len(row)
        a = ref[i - 1]
        for j in range(1, len(row) + 1):
            cur[j] = min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a != row[j - 1]))
        prev = cur
    return min(prev)


def measure(ref_lines, rows):
    chars = errors = words = wrong = missed = 0
    detail = []
    for ref in ref_lines:
        r = squash(ref)
        if not r:
            continue
        best, best_row = len(r), ''
        for row in rows:
            d = infix_distance(r, row)
            if d < best:
                best, best_row = d, row
        chars += len(r)
        errors += best
        ws = r.split(' ')
        row_words = set(best_row.split(' '))
        bad = sum(1 for w in ws if w not in row_words and w not in best_row)
        words += len(ws)
        wrong += bad
        if best * 2 > len(r):
            missed += 1
        detail.append((best, len(r), r, best_row))
    return chars, errors, words, wrong, missed, detail


def main():
    ref_path, root = Path(sys.argv[1]), Path(sys.argv[2])
    show = '--lines' in sys.argv
    blocks, name = {}, None
    for line in ref_path.read_text(encoding='utf-8').splitlines():
        if line.startswith('#'):
            continue
        if line.startswith('== '):
            name = line[3:].strip()
            blocks[name] = []
        elif name is not None and line.strip():
            blocks[name].append(line)
    total = [0, 0, 0, 0, 0, 0]
    per_set = {}
    print(f'{"screen":28} {"chars":>6} {"errors":>6} {"CER %":>7} {"words":>6} {"wrong":>6} {"WER %":>7} {"lines":>6} {"missed":>6}')
    for name, lines in blocks.items():
        g = root / (name + '.grid')
        if not g.exists():
            print(f'{name:28} (no grid in {root})')
            continue
        c, e, w, x, m, detail = measure(lines, grid_rows(g))
        print(f'{name:28} {c:6d} {e:6d} {100*e/c:7.2f} {w:6d} {x:6d} {100*x/w:7.2f} {len(detail):6d} {m:6d}')
        if show:
            for d, n, r, row in detail:
                if d:
                    print(f'      {d:3d}/{n:<3d} want: {r}\n              got:  {row[:150]}')
        s = per_set.setdefault(name.split('/')[0], [0, 0, 0, 0, 0, 0])
        for acc in (total, s):
            acc[0] += c; acc[1] += e; acc[2] += w; acc[3] += x; acc[4] += len(detail); acc[5] += m
    print()
    for k, s in list(per_set.items()) + [('ALL', total)]:
        if s[0]:
            print(f'{k:28} {s[0]:6d} {s[1]:6d} {100*s[1]/s[0]:7.2f} {s[2]:6d} {s[3]:6d} {100*s[3]/s[2]:7.2f} {s[4]:6d} {s[5]:6d}')


if __name__ == '__main__':
    main()
