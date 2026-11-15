#!/usr/bin/env bash
# fuzz_diff.sh -- differential testing on random circuits.
#
# For seeds FIRST..FIRST+N-1: generate a random circuit (tests/gen_aig.py)
# and a random pattern file, then
#   - run several command flows through our binary AND the reference binary
#     (scripts/compare.sh): outputs must be identical;
#   - run fraig flows through our binary only and check with CIRVerify that
#     the result is still equivalent to the original circuit.
# A failing circuit is kept as /tmp/fuzz_<seed>.aag for debugging.
#
# Usage:  tests/fuzz_diff.sh <N> [FIRST=1]
# Env:    MAXAND  max AND gates per circuit (default 40)
#         NOREF=1 skip the comparison with the reference binary; only the
#                 CIRVerify checks run.  ctest runs it this way (no ref/ needed).
set -u
cd "$(dirname "$0")/.." || exit 2
N="${1:?number of circuits}"; FIRST="${2:-1}"
if [ -z "${MINE:-}" ]; then
   if [ -x build/fraig ]; then MINE=build/fraig; else MINE=bin/fraig; fi
fi
export MINE
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
aag="$TMP/c.aag"; pat="$TMP/c.pat"

fails=0
for seed in $(seq "$FIRST" $((FIRST + N - 1))); do
   python3 tests/gen_aig.py "$aag" "$seed" "${MAXAND:-40}"
   read -r _ M I _ O _ < "$aag"
   # random pattern file: 0..150 patterns of I bits
   python3 -c "
import random; r = random.Random($seed)
print('\n'.join(''.join(r.choice('01') for _ in range($I)) for _ in range(r.randint(0, 150))))" > "$pat"
   # a few gates to report (ids may be PIs, ANDs, POs, undefined or unused)
   g1=$(( seed % (M + O + 1) )); g2=$(( (seed * 7 + 3) % (M + O + 1) ))
   flows=(
      "cirp; cirp -n; cirp -pi; cirp -po; cirp -fl; cirg $g1; cirg $g1 -fi 3; cirg $g2 -fo 3"
      "cirsw; cirp; cirp -n; cirp -fl"
      "ciropt; cirp; cirp -n; cirp -fl; cirg $g2 -fi 2"
      "cirstrash; cirp; cirp -n; cirp -fl"
      "ciropt; cirstrash; cirsw; ciropt; cirp -n; cirp -fl; cirw"
      "cirsim -f $pat; cirp -fec; cirg $g1"
      "cirstrash; cirsim -f $pat; cirp -fec"
   )
   [ -n "${NOREF:-}" ] && flows=()
   for f in "${flows[@]+"${flows[@]}"}"; do
      if ! out="$(bash scripts/compare.sh "$f" "$aag" 2>&1)"; then
         echo "DIFF seed $seed flow \"$f\""; echo "$out" | tail -n +2 | head -12 | sed 's/^/    /'
         cp "$aag" "/tmp/fuzz_$seed.aag"; cp "$pat" "/tmp/fuzz_$seed.pat"; fails=$((fails + 1))
      fi
   done
   for f in "cirsim -r; cirfraig" "cirstrash; cirsim -f $pat; cirfraig; cirsw; ciropt" \
            "ciropt; cirstrash; cirsim -r; cirfraig; cirsim -r; cirfraig; cirsw"; do
      printf 'cirr %s\n%s\ncirv\nq -f\n' "$aag" "$(echo "$f" | tr ';' '\n')" > "$TMP/v.dof"
      if ! timeout 60 "$MINE" -f "$TMP/v.dof" 2>&1 | grep -q "Result: EQUIVALENT"; then
         echo "NOT EQUIVALENT seed $seed flow \"$f\""
         cp "$aag" "/tmp/fuzz_$seed.aag"; cp "$pat" "/tmp/fuzz_$seed.pat"; fails=$((fails + 1))
      fi
   done
done
echo "---- $N circuits (seeds $FIRST..$((FIRST + N - 1))), $fails failures"
[ "$fails" -eq 0 ]
