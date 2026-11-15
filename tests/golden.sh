#!/usr/bin/env bash
# golden.sh -- golden-output (characterization) tests for fraig.
#
# A test case is one line of tests/cases.txt:
#     name | circuit.aag | cmd1; cmd2; ...
# We feed "cirr <circuit>", the commands and "q -f" to the binary, normalize
# the output (scripts/normalize.sh) and compare it with tests/golden/<name>.out
#
# Usage:
#   tests/golden.sh run    <fraig-binary> <case-name>   # exit 0 iff output matches
#   tests/golden.sh update <fraig-binary> [case-name...] # (re)write golden files
#
# The golden files were produced by our binary, after scripts/compare.sh
# showed that the non-"fraig-" cases are identical to the reference binary and
# scripts/check_equiv.sh showed that the "fraig-" cases are equivalent to the
# original circuit.  See tests/README.md.

set -u
cd "$(dirname "$0")/.." || exit 2

mode="${1:-}"; bin="${2:-}"; shift 2 2>/dev/null
if [ -z "$mode" ] || [ -z "$bin" ]; then
   sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 2
fi
bin="$(cd "$(dirname "$bin")" && pwd)/$(basename "$bin")"

# Print "name|circuit|commands" lines (comments and blanks removed, spaces trimmed).
read_cases() {
   grep -Ev '^[[:space:]]*(#|$)' tests/cases.txt | sed 's/[[:space:]]*|[[:space:]]*/|/g; s/^[[:space:]]*//; s/[[:space:]]*$//'
}

# run_case <circuit> <commands>  ->  normalized output on stdout
run_case() {
   local dof; dof="$(mktemp)"
   {
      echo "cirr $1"
      echo "$2" | tr ';' '\n' | sed 's/^ *//; s/ *$//' | grep -v '^$'
      echo "q -f"
   } > "$dof"
   timeout 300 "$bin" -f "$dof" 2>&1 | bash scripts/normalize.sh
   rm -f "$dof"
}

status=0
found=0
while IFS='|' read -r name circuit cmds; do
   if [ "$#" -gt 0 ]; then
      case " $* " in *" $name "*) ;; *) continue ;; esac
   fi
   found=1
   case "$mode" in
      update)
         run_case "$circuit" "$cmds" > "tests/golden/$name.out"
         echo "updated tests/golden/$name.out"
         ;;
      run)
         if [ ! -f "tests/golden/$name.out" ]; then
            echo "missing golden file tests/golden/$name.out"; status=1; continue
         fi
         actual="$(mktemp)"
         run_case "$circuit" "$cmds" > "$actual"
         if ! diff -u "tests/golden/$name.out" "$actual" | head -40; then :; fi
         cmp -s "tests/golden/$name.out" "$actual" || status=1
         rm -f "$actual"
         ;;
      *) echo "unknown mode '$mode'"; exit 2 ;;
   esac
done < <(read_cases)

[ "$found" -eq 1 ] || { echo "no such test case: $*"; exit 2; }
exit $status
