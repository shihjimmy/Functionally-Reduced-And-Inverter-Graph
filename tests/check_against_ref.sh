#!/usr/bin/env bash
# check_against_ref.sh -- prove that the golden files are right: run every
# non-"fraig-", non-"verify-" case of tests/cases.txt through scripts/compare.sh, i.e. compare
# our binary with the reference binary in ref/.
#
# Usage:  tests/check_against_ref.sh        (needs ref/ and a built binary)
# Env:    REF, MINE as for scripts/compare.sh
set -u
cd "$(dirname "$0")/.." || exit 2

fail=0
while IFS='|' read -r name circuit cmds; do
   name="$(echo "$name" | tr -d ' ')"
   case "$name" in fraig-*|verify-*) continue ;; esac
   circuit="$(echo "$circuit" | sed 's/^ *//; s/ *$//')"
   cmds="$(echo "$cmds" | sed 's/^ *//; s/ *$//')"
   # "cirr" is added by compare.sh; random-sim pattern counts are implementation-defined
   if out="$(IGNORE='patterns simulated' bash scripts/compare.sh "$cmds" "$circuit" 2>&1)"; then
      echo "PASS  $name"
   else
      echo "FAIL  $name"; echo "$out" | sed 's/^/      /' | head -20; fail=1
   fi
done < <(grep -Ev '^[[:space:]]*(#|$)' tests/cases.txt)
exit $fail
