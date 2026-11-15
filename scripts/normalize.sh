#!/usr/bin/env bash
# normalize.sh -- filter fraig output so that two runs can be compared.
#
# Reads stdout+stderr of a fraig run on stdin, writes the comparable part to
# stdout.  Used by scripts/compare.sh and tests/golden.sh.
#
# Lines that legitimately differ between runs / between implementations:
#  - debug messages of sweep / optimize / strash / fraig (the spec says they
#    are optional and are filtered out in grading)
#  - timing and memory reports of the 'usage' command
#  - IGNORE (optional env var): an extra regex, e.g. 'patterns simulated'
#    for random simulation, whose pattern count is implementation-defined
#
# Progress text is overwritten on screen with '\r'; only the text after the
# last '\r' of each line is kept, i.e. what the user would have seen.

FILTER='^(Sweeping:|Simplifying:|Strashing:|Fraig:|Updating|Merging:|Period time used|Total time used|Total memory used)'
[ -n "${IGNORE:-}" ] && FILTER="$FILTER|${IGNORE}"

sed 's/.*\r//' | grep -Ev "$FILTER"
exit 0
