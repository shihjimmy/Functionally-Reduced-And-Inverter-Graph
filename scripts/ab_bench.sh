#!/usr/bin/env bash
# ab_bench.sh -- A/B timing of two fraig binaries on the same flow.
#
# Runs OLD and NEW alternately (OLD, NEW, OLD, NEW, ...) so that both see the
# same machine noise, then prints every time and the medians.  Use this to
# decide "did my change make it slower?": absolute numbers from different
# runs of scripts/bench.py drifted by 10-18% on the same binary.
#
# Usage:  scripts/ab_bench.sh <old-binary> <new-binary> <circuit.aag> "<cmd1; cmd2; ...>" [runs=5]
# Example (keep a copy of the old build first):
#   cp build/fraig /tmp/fraig-old; <change code>; cmake --build build
#   scripts/ab_bench.sh /tmp/fraig-old build/fraig tests.fraig/sim07.aag "cirsim -r; cirfraig; cirsw; ciropt"
set -u
old="$1"; new="$2"; circuit="$3"; flow="$4"; runs="${5:-5}"
dof="$(mktemp)"; trap 'rm -f "$dof"' EXIT
{ echo "cirr $circuit"; echo "$flow" | tr ';' '\n' | sed 's/^ *//'; echo "q -f"; } > "$dof"

t_old=(); t_new=()
for i in $(seq 1 "$runs"); do
   for which in old new; do
      bin="$old"; [ $which = new ] && bin="$new"
      s=$(date +%s.%N); "$bin" -f "$dof" > /dev/null 2>&1; e=$(date +%s.%N)
      t=$(echo "$e - $s" | bc)
      if [ $which = old ]; then t_old+=("$t"); else t_new+=("$t"); fi
   done
done
median() { printf '%s\n' "$@" | sort -n | awk '{a[NR]=$1} END{print (NR%2 ? a[(NR+1)/2] : (a[NR/2]+a[NR/2+1])/2)}'; }
printf 'old: %s\n' "$(printf '%.2f ' "${t_old[@]}")"
printf 'new: %s\n' "$(printf '%.2f ' "${t_new[@]}")"
mo=$(median "${t_old[@]}"); mn=$(median "${t_new[@]}")
printf 'median old %.2f s, new %.2f s, new/old = %.2f\n' "$mo" "$mn" "$(echo "$mn / $mo" | bc -l)"
