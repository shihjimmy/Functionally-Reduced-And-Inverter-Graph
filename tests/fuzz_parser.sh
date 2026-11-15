#!/usr/bin/env bash
# fuzz_parser.sh -- parser fuzzing against the reference binary.
#
# Damages valid circuits (tests/corrupt_aag.py) and checks that our parser
# prints exactly the same error messages (or the same circuit) as the
# reference: "cirr <bad file>; cirp; cirp -n; cirp -fl".  Run it with a
# sanitizer build (MINE=build-san/fraig) to also catch crashes and leaks.
# Failing inputs are kept as /tmp/parse_<name>_<seed>.aag.
#
# Usage:  tests/fuzz_parser.sh <seeds-per-file> <file.aag>...   (needs ref/)
set -u
cd "$(dirname "$0")/.." || exit 2
N="${1:?seeds}"; shift
if [ -z "${MINE:-}" ]; then
   if [ -x build/fraig ]; then MINE=build/fraig; else MINE=bin/fraig; fi
fi
export MINE
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
fails=0; total=0
for aag in "$@"; do
   for seed in $(seq 1 "$N"); do
      what="$(python3 tests/corrupt_aag.py "$aag" "$TMP/bad.aag" "$seed")"
      total=$((total + 1))
      if ! out="$(bash scripts/compare.sh "cirp; cirp -n; cirp -fl" "$TMP/bad.aag" 2>&1)"; then
         fails=$((fails + 1))
         keep="/tmp/parse_$(basename "$aag" .aag)_$seed.aag"; cp "$TMP/bad.aag" "$keep"
         echo "DIFF $keep ($what)"; echo "$out" | grep -E '^[-+][^-+]' | head -6 | sed 's/^/    /'
      fi
   done
done
echo "---- $total damaged files, $fails differ"
[ "$fails" -eq 0 ]
