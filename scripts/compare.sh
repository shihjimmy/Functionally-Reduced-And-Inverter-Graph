#!/usr/bin/env bash
# compare.sh -- feed the same commands to the reference binary and to ours,
# then diff the two outputs.
#
# Usage:
#   bash scripts/compare.sh "<cmd1; cmd2; ...>" <file.aag> [more.aag ...]
#
# Example:
#   bash scripts/compare.sh "cirp -n; cirsw; cirp -n" tests.fraig/opt0*.aag
#
# For every .aag file the script writes a dofile:
#     cirr <file.aag>
#     <your commands, one per line>
#     q -f
# runs it through both binaries, and reports PASS / FAIL per file.
#
# Environment variables (all optional):
#   REF    reference binary   (default: ref/fraig-linux18, or ref/fraig-mac on macOS)
#   MINE   binary under test  (default: build/fraig, else bin/fraig)
#   SHOW   diff lines to print per failing file (default: 15)
#   IGNORE extra regex of output lines to ignore (default: none)

set -u
cd "$(dirname "$0")/.." || exit 2

if [ $# -lt 2 ]; then
   sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
   exit 2
fi
CMDS="$1"; shift

case "$(uname -s)" in
   Darwin) REF="${REF:-ref/fraig-mac}" ;;
   *)      REF="${REF:-ref/fraig-linux18}" ;;
esac
# our binary: the CMake build (build/fraig) if present, else the old Makefile one
if [ -z "${MINE:-}" ]; then
   if [ -x build/fraig ]; then MINE=build/fraig; else MINE=bin/fraig; fi
fi
SHOW="${SHOW:-15}"

for b in "$REF" "$MINE"; do
   [ -f "$b" ] || { echo "Error: $b not found (did you build? see README)"; exit 2; }
   [ -x "$b" ] || chmod +x "$b" 2>/dev/null
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0
for aag in "$@"; do
   {
      echo "cirr $aag"
      echo "$CMDS" | tr ';' '\n' | sed 's/^ *//; s/ *$//' | grep -v '^$'
      echo "q -f"
   } > "$TMP/in.dof"

   timeout 120 "$REF"  -f "$TMP/in.dof" 2>&1 | bash scripts/normalize.sh > "$TMP/ref.out"
   timeout 120 "$MINE" -f "$TMP/in.dof" 2>&1 | bash scripts/normalize.sh > "$TMP/mine.out"

   if cmp -s "$TMP/ref.out" "$TMP/mine.out"; then
      echo "PASS  $aag"; pass=$((pass + 1))
   else
      echo "FAIL  $aag"; fail=$((fail + 1))
      diff -u "$TMP/ref.out" "$TMP/mine.out" | sed -n "1,${SHOW}p"
   fi
done

echo "---- $pass passed, $fail failed"
[ "$fail" -eq 0 ]
