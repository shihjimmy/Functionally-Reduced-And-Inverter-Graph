#!/usr/bin/env bash
# check_equiv.sh -- prove that our optimized circuit is functionally
# equivalent to the original, using the reference program as the checker.
#
# Usage:
#   bash scripts/check_equiv.sh "<commands>" <file.aag> [more.aag ...]
#
# For each file: run bin/fraig with "cirr <file>; <commands>; cirw -o out",
# then let the reference build a miter of <file> and out ("CIRMiter", a
# hidden command of the reference program) and fraig it. The circuits are
# equivalent iff every miter output is driven by CONST 0 afterwards.
# TIMEOUT (env, seconds, default 300) limits each reference run.

set -u
cd "$(dirname "$0")/.." || exit 2
[ $# -ge 2 ] || { sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
CMDS="$1"; shift
case "$(uname -s)" in
   Darwin) REF="${REF:-ref/fraig-mac}" ;;
   *)      REF="${REF:-ref/fraig-linux18}" ;;
esac
if [ -z "${MINE:-}" ]; then
   if [ -x build/fraig ]; then MINE=build/fraig; else MINE=bin/fraig; fi
fi
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0
for aag in "$@"; do
   { echo "cirr $aag"; echo "$CMDS" | tr ';' '\n' | sed 's/^ *//; s/ *$//' | grep -v '^$'
     echo "cirw -o $TMP/mine.aag"; echo "q -f"; } > "$TMP/opt.dof"
   timeout 300 "$MINE" -f "$TMP/opt.dof" > /dev/null 2>&1
   printf 'cirmiter %s %s\ncirsim -r\ncirfraig\ncirp -n\nq -f\n' "$aag" "$TMP/mine.aag" > "$TMP/miter.dof"
   # After fraig, a proven miter output reads "PO <id> 0"; anything else
   # (an AIG fanin, or "!0" = constant 1) means "not proven equal".
   # No PO line at all means the checker did not finish (e.g. timeout).
   timeout "${TIMEOUT:-300}" "$REF" -f "$TMP/miter.dof" 2>&1 | sed 's/.*\r//' \
         | grep -E '^\[[0-9]+\] PO ' > "$TMP/pos.txt"
   npo=$(wc -l < "$TMP/pos.txt")
   bad=$(grep -vcE 'PO +[0-9]+ 0( |$)' "$TMP/pos.txt")
   if [ ! -s "$TMP/mine.aag" ] || [ "$npo" -eq 0 ]; then
      echo "UNKNOWN $aag (no result: timeout or crash)"; fail=$((fail + 1))
   elif [ "$bad" -eq 0 ]; then
      echo "EQUIV $aag"; pass=$((pass + 1))
   else
      echo "DIFF  $aag ($bad of $npo miter outputs not proven 0)"; fail=$((fail + 1))
   fi
done
echo "---- $pass equivalent, $fail not proven"
[ "$fail" -eq 0 ]
