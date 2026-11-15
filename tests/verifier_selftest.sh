#!/usr/bin/env bash
# verifier_selftest.sh -- check the checker.
#
# For each circuit and seed: break the circuit a little (tests/mutate.py),
# ask our CIRVerify whether it is still equivalent, and ask the reference
# binary the same question (a CIRMiter + fraig, as in scripts/check_equiv.sh).
# Both must agree.  When ours reports a counter-example, the reference
# binary also simulates it on both circuits and the outputs must differ.
#
# Usage:  tests/verifier_selftest.sh <seeds-per-circuit> <file.aag>...
# Needs ref/ (this is a debugging tool, not part of ctest).
set -u
cd "$(dirname "$0")/.." || exit 2
N="${1:?seeds}"; shift
REF="${REF:-ref/fraig-linux18}"
if [ -z "${MINE:-}" ]; then
   if [ -x build/fraig ]; then MINE=build/fraig; else MINE=bin/fraig; fi
fi
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

agree=0; disagree=0; ndiff=0
for aag in "$@"; do
  for seed in $(seq 1 "$N"); do
    python3 tests/mutate.py "$aag" "$TMP/m.aag" "$seed" > /dev/null
    # ours
    printf 'cirr %s\ncirv -f %s\nq -f\n' "$aag" "$TMP/m.aag" > "$TMP/v.dof"
    "$MINE" -f "$TMP/v.dof" 2>&1 | sed 's/.*\r//' > "$TMP/mine.out"
    if grep -q "Result: EQUIVALENT" "$TMP/mine.out"; then mine=EQ
    elif grep -q "Result: NOT EQUIVALENT" "$TMP/mine.out"; then mine=NE
    else mine=ERR; fi
    # reference
    printf 'cirmiter %s %s\ncirsim -r\ncirfraig\ncirp -n\nq -f\n' "$aag" "$TMP/m.aag" > "$TMP/r.dof"
    timeout 300 "$REF" -f "$TMP/r.dof" 2>&1 | sed 's/.*\r//' | grep -E '^\[[0-9]+\] PO ' > "$TMP/pos.txt"
    if [ ! -s "$TMP/pos.txt" ]; then ref=ERR
    elif grep -vqE 'PO +[0-9]+ 0( |$)' "$TMP/pos.txt"; then ref=NE
    else ref=EQ; fi

    # replay our first counter-example on the reference simulator
    cex=ok
    if [ "$mine" = NE ]; then
      line="$(grep -m1 'differs for PI values' "$TMP/mine.out")"
      out="$(echo "$line" | sed -E 's/.*Output ([0-9]+) .*/\1/')"
      pis="$(echo "$line" | sed -E 's/.*first\): ([01]*|\(none\)).*/\1/')"
      if ! echo "$line" | grep -q 'undefined' && [ "$pis" != "(none)" ]; then
        echo "$pis" > "$TMP/pat"
        for c in a b; do
          f="$aag"; [ $c = b ] && f="$TMP/m.aag"
          printf 'cirr %s\ncirsim -f %s -o %s\nq -f\n' "$f" "$TMP/pat" "$TMP/$c.log" > "$TMP/s.dof"
          "$REF" -f "$TMP/s.dof" > /dev/null 2>&1
        done
        oa="$(awk '{print $2}' "$TMP/a.log" | cut -c$((out + 1)))"
        ob="$(awk '{print $2}' "$TMP/b.log" | cut -c$((out + 1)))"
        [ -n "$oa" ] && [ "$oa" != "$ob" ] || cex=BAD
      fi
    fi

    if [ "$mine" = "$ref" ] && [ "$cex" = ok ] && [ "$mine" != ERR ]; then
      agree=$((agree + 1)); [ "$mine" = NE ] && ndiff=$((ndiff + 1))
    else
      disagree=$((disagree + 1))
      echo "DISAGREE $aag seed $seed: ours=$mine ref=$ref counter-example=$cex"
      cp "$TMP/m.aag" "/tmp/selftest_$(basename "$aag" .aag)_$seed.aag"
    fi
  done
done
echo "---- $agree agree ($ndiff not equivalent, $((agree - ndiff)) equivalent), $disagree disagree"
[ "$disagree" -eq 0 ]
