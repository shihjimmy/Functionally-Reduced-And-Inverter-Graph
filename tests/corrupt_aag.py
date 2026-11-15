#!/usr/bin/env python3
"""corrupt_aag.py -- write a syntactically damaged copy of an .aag file.

Usage: corrupt_aag.py <in.aag> <out.aag> <seed>

One random edit, aimed at the parser's error messages (spec: "missing
space", "extra space", "illegal white space", "missing number", "number too
big", "cannot redefine", "illegal identifier", ...).  Prints what it did.
"""
import random, sys

src, dst, seed = sys.argv[1], sys.argv[2], int(sys.argv[3])
rnd = random.Random(seed)
text = open(src).read()
lines = text.split("\n")
body = [i for i, l in enumerate(lines) if l and not l.startswith("c")]
# stop at the comment section
cut = next((i for i, l in enumerate(lines) if l == "c"), len(lines))
body = [i for i in body if i < cut] or [0]

kind = rnd.choice(["delete-char", "insert-space", "insert-tab", "double-space",
                   "leading-space", "trailing-space", "letter-for-digit",
                   "big-number", "negative", "delete-line", "duplicate-line",
                   "swap-lines", "header-number", "truncate", "blank-line",
                   "odd-literal", "symbol-error", "drop-newline-at-end"])
i = rnd.choice(body)
l = lines[i]

def pos(s):
    return rnd.randint(0, max(len(s) - 1, 0))

if kind == "delete-char" and l:
    p = pos(l); lines[i] = l[:p] + l[p + 1:]
elif kind == "insert-space":
    p = pos(l); lines[i] = l[:p] + " " + l[p:]
elif kind == "insert-tab":
    p = pos(l); lines[i] = l[:p] + "\t" + l[p:]
elif kind == "double-space" and " " in l:
    lines[i] = l.replace(" ", "  ", 1)
elif kind == "leading-space":
    lines[i] = " " + l
elif kind == "trailing-space":
    lines[i] = l + " "
elif kind == "letter-for-digit":
    ds = [k for k, c in enumerate(l) if c.isdigit()]
    if ds:
        p = rnd.choice(ds); lines[i] = l[:p] + rnd.choice("xO-+") + l[p + 1:]
elif kind == "big-number":
    toks = l.split(" ")
    k = rnd.randrange(len(toks)); toks[k] = str(rnd.choice([99999, 2**31, 2**40, 4294967297])); lines[i] = " ".join(toks)
elif kind == "negative":
    toks = l.split(" ")
    k = rnd.randrange(len(toks))
    if toks[k].isdigit(): toks[k] = "-" + toks[k]
    lines[i] = " ".join(toks)
elif kind == "delete-line":
    del lines[i]
elif kind == "duplicate-line":
    lines.insert(i, l)
elif kind == "swap-lines" and len(body) > 1:
    j = rnd.choice(body); lines[i], lines[j] = lines[j], lines[i]
elif kind == "header-number":
    toks = lines[0].split(" ")
    k = rnd.randrange(1, len(toks))
    if toks[k].isdigit(): toks[k] = str(max(0, int(toks[k]) + rnd.choice([-2, -1, 1, 2])))
    lines[0] = " ".join(toks)
elif kind == "truncate":
    t = "\n".join(lines); lines = t[:rnd.randint(0, len(t))].split("\n")
elif kind == "blank-line":
    lines.insert(i + 1, "")
elif kind == "odd-literal" and i > 0:
    toks = l.split(" ")
    if toks[0].isdigit(): toks[0] = str(int(toks[0]) | 1)
    lines[i] = " ".join(toks)
elif kind == "symbol-error":
    lines.insert(cut, rnd.choice(["i0 dup", "i99 x", "o0", "x1 y", "i0 a\tb", "l0 q", "i0 "]))
    if rnd.random() < 0.5: lines.insert(cut, "i0 first")
elif kind == "drop-newline-at-end":
    pass

out = "\n".join(lines)
if kind == "drop-newline-at-end":
    out = out.rstrip("\n")
open(dst, "w").write(out)
print(kind, "line", i + 1)
