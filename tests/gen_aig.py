#!/usr/bin/env python3
"""gen_aig.py -- write a random combinational AIGER (aag) file.

Usage: gen_aig.py <out.aag> <seed> [max-ands]

The circuit is small (so a diff is easy to read) but deliberately exercises
the corner cases of the project:
  - constant fanins (literals 0 and 1), both phases of every signal
  - duplicated and complementary fanins (a & a, a & !a): optimize rules
  - structurally identical AND gates: strash
  - functionally equivalent but structurally different gates: fraig
  - undefined fanins (variables never defined), unused gates and unused PIs
  - AND gates listed in any order, outputs on PIs or constants, symbols

The same seed always gives the same file.
"""
import random, sys

out, seed = sys.argv[1], int(sys.argv[2])
max_ands = int(sys.argv[3]) if len(sys.argv) > 3 else 40
rnd = random.Random(seed)

n_in = rnd.randint(1, 8)
n_and = rnd.randint(0, max_ands)
n_out = rnd.randint(1, 5)
n_undef = rnd.choice([0, 0, 0, 1, 2])          # most circuits have none

in_vars = list(range(1, n_in + 1))
and_vars = list(range(n_in + 1, n_in + n_and + 1))
undef_vars = list(range(n_in + n_and + 1, n_in + n_and + n_undef + 1))
max_var = n_in + n_and + n_undef

ands = []                                       # (lhs var, lit, lit), topological
available = [2 * v for v in in_vars]            # literals usable as fanins (positive)


def pick():
    r = rnd.random()
    if r < 0.04:
        return rnd.choice([0, 1])               # constant
    if r < 0.08 and undef_vars:
        return 2 * rnd.choice(undef_vars) + rnd.randint(0, 1)
    # prefer recent signals: gives deeper circuits
    k = len(available)
    i = k - 1 - min(int(rnd.expovariate(0.3)), k - 1)
    return available[i] + rnd.randint(0, 1)


for v in and_vars:
    r = rnd.random()
    if r < 0.08 and ands:                       # copy an existing gate: strash target
        _, a, b = rnd.choice(ands)
        if rnd.random() < 0.5:
            a, b = b, a
    elif r < 0.14:                              # a & a  or  a & !a
        a = pick()
        b = a if rnd.random() < 0.5 else a ^ 1
    elif r < 0.22 and ands:                     # x & g, where g = x & y: equals g
        g, x, _ = rnd.choice(ands)              # (fraig target: same function as g,
        a, b = x, 2 * g                         #  different structure)
    else:
        a, b = pick(), pick()
    ands.append((v, a, b))
    available.append(2 * v)

outputs = []
for _ in range(n_out):
    r = rnd.random()
    if r < 0.05:
        outputs.append(rnd.choice([0, 1]))
    elif r < 0.15:
        outputs.append(2 * rnd.choice(in_vars) + rnd.randint(0, 1))
    else:
        outputs.append(pick())

order = ands[:]
if rnd.random() < 0.5:
    rnd.shuffle(order)                          # AIGER allows any order

lines = ["aag %d %d 0 %d %d" % (max_var, n_in, n_out, n_and)]
lines += [str(2 * v) for v in in_vars]
lines += [str(o) for o in outputs]
lines += ["%d %d %d" % (2 * v, a, b) for (v, a, b) in order]
if rnd.random() < 0.5:                          # symbols for some PIs / POs
    for i in range(n_in):
        if rnd.random() < 0.5:
            lines.append("i%d in%d" % (i, i))
    for i in range(n_out):
        if rnd.random() < 0.5:
            lines.append("o%d out%d" % (i, i))
if rnd.random() < 0.5:
    lines += ["c", "random circuit, seed %d" % seed]
open(out, "w").write("\n".join(lines) + "\n")
