#!/bin/sh
# Play test a section of Super Metroid without sitting through the intro.
#
#   ./playtest.sh [--debug] [--keep] [SECTION] [smxsave options...]
#   ./playtest.sh --list
#
# Builds the Debug shell, writes a save at SECTION's save station with its
# loadout (build/playtest/SECTION), and starts the game in play from it.
# SECTION defaults to landing. smxsave options such as --items screw,speed
# or --energy 999 change the loadout; ./playtest.sh --list shows them.
#
#   --debug  run under lldb or gdb (see make debug)
#   --keep   reuse the section's save and state, to continue a session
#
# Saves and quick-saves (F5/F9) in a play test stay in build/playtest/SECTION,
# away from your own saves. ROM overrides the ROM path (default SuperMetroid.sfc).
set -eu

cd "$(dirname "$0")"
ROM=${ROM:-SuperMetroid.sfc}
TARGET=run
KEEP=0
SECTION=landing

while [ $# -gt 0 ]; do
  case "$1" in
    --debug) TARGET=debug; shift ;;
    --keep) KEEP=1; shift ;;
    --list|--help|-h) make build >/dev/null; exec build/debug/smxsave --list ;;
    -*) break ;;
    *) SECTION=$1; shift; break ;;
  esac
done

if [ ! -f "$ROM" ]; then
  echo "playtest: $ROM not found; put the ROM there or set ROM=path" >&2
  exit 1
fi

make build
DIR=build/playtest/$SECTION
if [ "$KEEP" = 0 ] || [ ! -f "$DIR/$(basename "${ROM%.*}").state" ]; then
  build/debug/smxsave --section "$SECTION" "$@" "$ROM" "$DIR"
fi
SMX_DATA_DIR=$DIR exec make "$TARGET" ROM="$ROM" ARGS=--load-state
