#!/usr/bin/env python3
"""bench.py -- time and memory of fraig flows, ours vs the reference.

Usage:
  python3 scripts/bench.py [--runs N] [--quick] [--only REGEX] [--no-ref] [--mine BIN] [--ref BIN]

For every (circuit, flow) below, each binary is run N times (default 3) on
a dofile "cirr <circuit>; <flow>; q -f".  The table shows the median wall
time and the peak memory (max RSS) of the process, and the AIG count that
the last "cirp" in the flow reports.  Output is a Markdown table, ready
for docs/benchmarks.md.

Measure on an otherwise idle machine; numbers are only comparable within
one run of this script.
"""
import argparse, os, re, resource, statistics, subprocess, sys, tempfile, time

FULL = "cirsim -r; cirfraig; cirsw; ciropt; cirp"
CASES = [
    # (circuit, flow, label)
    ("tests.fraig/ISCAS85/C6288.aag", "cirstrash; cirp", "strash"),
    ("tests.fraig/sim13.aag",         "cirsw; ciropt; cirstrash; cirp", "sweep+opt+strash"),
    ("tests.fraig/sim13.aag",         "cirsim -r; cirp", "random sim"),
    ("tests.fraig/sim13.aag",         "cirsim -f tests.fraig/pattern.13; cirp", "file sim (76 MB)"),
    ("tests.fraig/sim06.aag",         FULL, "fraig flow"),
    ("tests.fraig/sim07.aag",         FULL, "fraig flow"),
    ("tests.fraig/sim12.aag",         FULL, "fraig flow"),
    ("tests.fraig/ISCAS85/C5315.aag", FULL, "fraig flow"),
    ("tests.fraig/ISCAS85/C7552.aag", FULL, "fraig flow"),
    ("tests.fraig/sim13.aag",         FULL, "fraig flow"),
    # ours only (the reference has no CIRVerify)
    ("tests.fraig/sim07.aag",         FULL + "; cirv", "fraig flow + verify"),
    ("tests.fraig/sim13.aag",         FULL + "; cirv", "fraig flow + verify"),
]
QUICK = {"tests.fraig/sim13.aag"}   # skipped with --quick


def run(binary, circuit, flow):
    """Returns (seconds, peak RSS in MB, AIG count or None)."""
    with tempfile.NamedTemporaryFile("w", suffix=".dof", delete=False) as f:
        f.write("cirr %s\n%s\nq -f\n" % (circuit, flow.replace("; ", "\n")))
        dof = f.name
    pid = os.fork()
    if pid == 0:
        out = os.open(os.devnull, os.O_WRONLY)
        os.dup2(out, 2)
        with open(dof + ".out", "w") as o:
            os.dup2(o.fileno(), 1)
            os.execv(binary, [binary, "-f", dof])
    t0 = time.time()
    _, _, ru = os.wait4(pid, 0)
    sec = time.time() - t0
    text = open(dof + ".out", errors="replace").read()
    os.unlink(dof); os.unlink(dof + ".out")
    aig = re.findall(r"^\s*AIG\s+(\d+)", text, re.M)
    return sec, ru.ru_maxrss / 1024.0, (int(aig[-1]) if aig else None)


def measure(binary, circuit, flow, runs):
    res = [run(binary, circuit, flow) for _ in range(runs)]
    return statistics.median(r[0] for r in res), max(r[1] for r in res), res[-1][2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--quick", action="store_true", help="skip sim13")
    ap.add_argument("--no-ref", action="store_true")
    ap.add_argument("--only", default="", help="regex on 'circuit flow-label'")
    ap.add_argument("--mine", default="build/fraig")
    ap.add_argument("--ref", default="ref/fraig-linux18")
    a = ap.parse_args()
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

    print("| circuit | flow | ref time | ours time | ref mem | ours mem | AIG ref / ours |")
    print("|---|---|---:|---:|---:|---:|---|")
    for circuit, flow, label in CASES:
        if a.quick and circuit in QUICK:
            continue
        if a.only and not re.search(a.only, "%s %s" % (circuit, label)):
            continue
        if "pattern.13" in flow and not os.path.exists("tests.fraig/pattern.13"):
            if not os.path.exists("tests.fraig/pattern.13.gz"):
                continue
            # stored gzipped in the repository (76 MB); unpack once
            subprocess.run(["gunzip", "-k", "tests.fraig/pattern.13.gz"], check=True)
        m = measure(a.mine, circuit, flow, a.runs)
        if a.no_ref or "cirv" in flow:
            r = None
        else:
            r = measure(a.ref, circuit, flow, a.runs)
        name = os.path.basename(circuit)[:-4]
        cell = lambda x, f: f % x if x is not None else "–"
        print("| %s | %s | %s | %s | %s | %s | %s / %s |" % (
            name, label,
            cell(r and r[0], "%.2f s"), "%.2f s" % m[0],
            cell(r and r[1], "%.0f MB"), "%.0f MB" % m[1],
            cell(r and r[2], "%d"), cell(m[2], "%d")))
        sys.stdout.flush()


if __name__ == "__main__":
    main()
