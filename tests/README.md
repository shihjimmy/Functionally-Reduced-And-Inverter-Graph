# Tests

`ctest --test-dir build -j4` runs one test per line of `cases.txt`.

Each case feeds `cirr <circuit>`, some commands and `q -f` to `fraig`, filters
the output (`scripts/normalize.sh`: drops timing/memory lines and the optional
debug messages) and compares it with `golden/<name>.out`.

## Where the golden files come from

They are produced by our own binary, and each kind is justified separately:

| cases | justification |
|---|---|
| everything except `fraig-*`, `verify-*` | identical to the reference binary: `tests/check_against_ref.sh` (needs `ref/`) |
| `fraig-*` | our fraig may merge more than the reference, so the output is not compared with it. Each case ends with `cirv` (CIRVerify), so the golden file contains `Result: EQUIVALENT`: the test proves the result equivalent by itself. Cross-checked once with `scripts/check_equiv.sh` (needs `ref/`) |
| `verify-*` | CIRVerify itself (the reference has no such command): equivalent and broken circuits, a counter-example that only SAT can find (`data/and20.aag` vs `data/zero20.aag`), undefined signals, error messages |

So the tests are a regression net that works without `ref/` (e.g. on CI), and
the reference binary is only needed when a golden file is *created*.

## Changing a test

```bash
tests/golden.sh run    build/fraig <case>      # one case
tests/golden.sh update build/fraig [<case>...] # rewrite golden files
```

Only run `update` after you have convinced yourself that the new output is
right (for non-fraig cases: `tests/check_against_ref.sh` passes; for `fraig-*`
cases: `check_equiv.sh` says EQUIV). Never update just to make a test pass.

## Checking the checker

`tests/verifier_selftest.sh <seeds> <file.aag>...` breaks each circuit a
little (`tests/mutate.py`), asks CIRVerify and the reference binary whether
it is still equivalent, and replays each counter-example on the reference
simulator. Both must agree. This needs `ref/` and is not part of ctest.

## Random circuits

`tests/gen_aig.py <out.aag> <seed> [max-ands]` writes a small random circuit
with the project's corner cases: constants, `a&a` / `a&!a`, copies of gates
(strash), `x & (x & y)` (fraig), undefined fanins, unused gates, gates in any
order, symbols.

- `tests/fuzz_diff.sh <N> [first-seed]` compares 7 command flows with the
  reference binary on N random circuits and checks 3 fraig flows with
  CIRVerify. `MAXAND=400` for bigger circuits. Needs `ref/`.
- `NOREF=1 tests/fuzz_diff.sh 200` runs only the CIRVerify checks; this is the
  ctest test `fuzz.equiv` (about 12 s).
- `tests/fuzz_parser.sh <seeds> <file.aag>...` damages valid files
  (`tests/corrupt_aag.py`: extra/missing spaces, tabs, bad numbers, missing or
  duplicated lines, truncation, bad symbols, ...) and compares the error
  messages with the reference. Use `MINE=build-san/fraig` to also catch crashes.

Last results: 1000 random circuits (up to 40 ANDs) + 200 (up to 400 ANDs),
no difference from the reference and every fraig result equivalent; 600
damaged files under ASan/UBSan, all error messages identical.

## Circuits the tests need

`tests.fraig/`: `opt01-07`, `strash01-10`, `sim01-08` with `pattern.01-08`,
`test1`, `test2`, `ISCAS85/C17`, `C432`, `C880`, and `tests/data/`. The 76 MB `pattern.13` is not needed.

## Unit tests (GoogleTest)

`tests/unit/` (built when `FRAIG_UNIT_TESTS=ON`, the default; CMake downloads
GoogleTest 1.12.1 with git at configure time):

- `test_gate.cpp`: `CirGateV` (null edge, phase, `~`, `^`), gate type tags,
  the fanin interface, `asAig()`; compile-time checks (`static_assert`) that
  an integer does not convert to an edge and that `CirAigGate` stays 64 bytes.
- `test_cirmgr.cpp`: reading (edges, fanouts, UNDEF gates, two managers at
  once, bad files), `CirMgr::checkInvariants()` notices broken netlists, and
  every editing command keeps the invariants on 15 test circuits and 300
  random circuits; the flow rules (`CirMgr::whyNot`), including "a fraig is
  not a CIRSTRash" and "each circuit has its own flow state".

Run one test: `build/tests/unit_tests --gtest_filter='CirGateV.*'`.
