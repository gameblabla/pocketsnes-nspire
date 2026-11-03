#!/bin/bash
# All the PC tests, with just the counts printed (details in build/ptest.log):
#   tools/perf/ptest.sh
set -u
cd "$(dirname "$0")/../.."
command -v arm-none-eabi-g++ >/dev/null || . "$HOME/nspire/build/env.sh" >/dev/null 2>&1
LOG=build/ptest.log
: > "$LOG"
ok=0
make -s unit-tests >> "$LOG" 2>&1 && echo "unit tests: $(grep -a 'checks,' "$LOG" | tail -1)" || { echo "unit tests: FAILED (see $LOG)"; ok=1; }
tests/run.sh >> "$LOG" 2>&1 && echo "PC tests:   $(grep -a 'passed,' "$LOG" | tail -1 | cut -d. -f1)" || { echo "PC tests:   FAILED: $(grep -a '^FAIL' "$LOG" | head -3 | tr '\n' ' ')"; ok=1; }
tests/asm/run.sh >> "$LOG" 2>&1 && echo "ARM tests:  $(grep -a 'passed,' "$LOG" | tail -1 | sed 's/^== //; s/ (.*//')" || { echo "ARM tests:  FAILED (see $LOG)"; ok=1; }
exit $ok
