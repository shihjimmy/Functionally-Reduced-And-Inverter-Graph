#!/usr/bin/env python3
"""mutate.py -- write a slightly broken copy of an .aag file.

Usage: mutate.py <in.aag> <out.aag> <seed>

One random mutation: flip the inversion of one AND fanin or of one output,
or replace one AND fanin by a PI or a constant.  The result is usually
(not always) a different function; tests/verifier_selftest.sh asks an
independent checker which it is.
"""
import random, sys

src, dst, seed = sys.argv[1], sys.argv[2], int(sys.argv[3])
rnd = random.Random(seed)
lines = open(src).read().split('\n')
M, I, L, O, A = map(int, lines[0].split()[1:6])
outs = list(range(1 + I + L, 1 + I + L + O))
ands = list(range(1 + I + L + O, 1 + I + L + O + A))
lits = [int(lines[k]) for k in range(1, 1 + I)]

kind = rnd.choice(['flip-and', 'swap-and', 'flip-out'] if ands else ['flip-out'])
if kind == 'flip-out':
    k = rnd.choice(outs)
    lines[k] = str(int(lines[k]) ^ 1)
else:
    k = rnd.choice(ands)
    lhs, a, b = map(int, lines[k].split())
    pos = rnd.choice([1, 2])
    if kind == 'flip-and':
        new = (a ^ 1, b) if pos == 1 else (a, b ^ 1)
    else:  # a PI or a constant: can never create a loop
        x = rnd.choice(lits[:I] + [0, 1]) ^ rnd.choice([0, 1])
        new = (x, b) if pos == 1 else (a, x)
    lines[k] = '%d %d %d' % (lhs, new[0], new[1])
open(dst, 'w').write('\n'.join(lines))
print(kind, 'line', k + 1)
