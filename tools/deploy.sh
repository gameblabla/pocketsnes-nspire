#!/bin/bash
# Builds PocketSNES for the calculator and puts it on it in one go, printing
# only a few lines:
#   tools/deploy.sh [--no-test] [--dest /snes/_PocketSNES.tns]
#   tools/deploy.sh --check    only reads the calculator's copy back and
#                              compares it with the last upload
# 1. PC tests (tests/run.sh, tests/asm/run.sh), unless --no-test; stops if
#    one fails. Their output goes to the log, only the pass counts are shown.
# 2. make -f Makefile.nspire, BUILD_NAME = the commit (+ "-dirty" when the
#    code has uncommitted changes), so the speed test's files say which
#    build wrote them.
# 3. Reads the calculator's copy twice (they must match) and keeps it in
#    ~/nspire/build/pocketsnes-nspire/tests/calc/versions/<date>-before-<commit>/.
# 4. Puts the old copy's settings area (the 8208-byte PSNESCFG trailer, see
#    frontend/selfstore.h) on the end of the new build.
# 5. Uploads it, reads it back and compares md5s (one more read on a
#    mismatch: a get once returned stray bytes).
# Everything else goes to build/deploy.log.
set -u
cd "$(dirname "$0")/.."
LINK=${LINK:-$HOME/nspire/build/tools/nspire-link}
VERSIONS=${VERSIONS:-$HOME/nspire/build/pocketsnes-nspire/tests/calc/versions}
DEST=/snes/_PocketSNES.tns
TEST=1
CHECK=0
while [ $# -gt 0 ]; do
    case $1 in
        --no-test) TEST=0 ;;
        --check) CHECK=1 ;;
        --dest) DEST=$2; shift ;;
        *) echo "usage: $0 [--no-test] [--check] [--dest /snes/_PocketSNES.tns]"; exit 2 ;;
    esac
    shift
done
LOG=build/deploy.log
TMP=build/deploy
mkdir -p "$TMP"
fail() { echo "FAILED: $*  (details: $LOG)"; exit 1; }

if [ $CHECK = 1 ]; then
    "$LINK" get "$DEST" "$TMP/check.tns" >> "$LOG" 2>&1 || fail "reading $DEST"
    cmp -s "$TMP/check.tns" "$TMP/new.tns" && echo "$DEST matches the last upload ($(md5sum < "$TMP/new.tns" | cut -c1-8))" \
        || fail "$DEST differs from the last upload"
    exit 0
fi
: > "$LOG"

. "$HOME/nspire/build/env.sh" >> "$LOG" 2>&1

"$LINK" info >> "$LOG" 2>&1 || fail "calculator not connected"

if [ $TEST = 1 ]; then
    tests/run.sh >> "$LOG" 2>&1 || fail "PC tests: $(grep -a 'passed,' "$LOG" | tail -1)"
    echo "PC tests:  $(grep -a 'passed,' "$LOG" | tail -1 | cut -d. -f1)"
    tests/asm/run.sh >> "$LOG" 2>&1 || fail "ARM tests: $(grep -a 'passed,' "$LOG" | tail -1)"
    echo "ARM tests: $(grep -a 'passed,' "$LOG" | tail -1 | sed 's/^== //; s/ (.*//')"
fi

NAME=$(git rev-parse --short HEAD)
git diff --quiet HEAD -- pocketsnes frontend Makefile.nspire || NAME=$NAME-dirty
rm -f build/nspire/frontend/emu.o   # BUILD_NAME is compiled into it
make -f Makefile.nspire -j"$(nproc)" BUILD_NAME="$NAME" >> "$LOG" 2>&1 || fail "build"

# The calculator's copy, read twice.
"$LINK" get "$DEST" "$TMP/old1.tns" >> "$LOG" 2>&1 || fail "reading $DEST"
"$LINK" get "$DEST" "$TMP/old2.tns" >> "$LOG" 2>&1 || fail "reading $DEST"
cmp -s "$TMP/old1.tns" "$TMP/old2.tns" || fail "the two reads of $DEST differ; run again"
BACKUP=$VERSIONS/$(date +%Y-%m-%d-%H%M)-before-$NAME
mkdir -p "$BACKUP" && cp "$TMP/old1.tns" "$BACKUP/_PocketSNES.tns"

# Its settings area on the end of the new build.
cp _PocketSNES.tns "$TMP/new.tns"
TRAILER=8208
if [ "$(tail -c 16 "$TMP/old1.tns" | head -c 8)" = PSNESCFG ]; then
    tail -c $TRAILER "$TMP/old1.tns" >> "$TMP/new.tns"
    SETTINGS="settings kept"
else
    SETTINGS="no settings area on the old copy (the saves folder's pocketsnes.cfg.tns is used)"
fi

"$LINK" put -f "$TMP/new.tns" "$DEST" >> "$LOG" 2>&1 || fail "upload"
WANT=$(md5sum < "$TMP/new.tns" | cut -c1-32)
GOT=none
# The calculator sometimes drops off USB for a few seconds right after an
# upload ("Invalid packet received", then "No device found").
for try in 1 2 3 4 5 6; do
    rm -f "$TMP/check.tns"
    if "$LINK" get "$DEST" "$TMP/check.tns" >> "$LOG" 2>&1; then
        GOT=$(md5sum < "$TMP/check.tns" | cut -c1-32)
        [ "$GOT" = "$WANT" ] && break
    else
        sleep 5
    fi
done
[ "$GOT" = "$WANT" ] || fail "read back md5 $GOT, uploaded $WANT (check: tools/deploy.sh --check); old copy is in $BACKUP"

echo "Uploaded $NAME to $DEST: $(stat -c %s "$TMP/new.tns") B, md5 ${WANT:0:8}, read back OK, $SETTINGS"
echo "Old copy: $BACKUP"
