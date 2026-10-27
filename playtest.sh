#!/bin/sh
# Play test a section of Super Metroid without sitting through the intro.
#
#   ./playtest.sh [before | after] [--dev | --debug] [--keep] [SECTION] [smxsave options...]
#   ./playtest.sh --list
#
# Builds the game, writes a save at SECTION's save station with its loadout
# (build/playtest/SECTION), and starts the game in play from it. It plays the
# Release build, fullscreen, so smoothness and sound are as players get them.
# SECTION defaults to explore: the ship, with every item. smxsave options
# such as --items screw,speed or --energy 999 change the loadout;
# ./playtest.sh --list shows them.
#
#   before     play main, before frame pacing and motion interpolation, built
#              from a worktree in build/before/src
#   after      play this checkout, with both (the default)
#   --dev      play the Debug build, in a window
#   --debug    play the Debug build under lldb or gdb (see make debug)
#   --keep     reuse the section's save and state, to continue a session
#
# Saves and quick-saves (F5/F9) in a play test stay in build/playtest/SECTION,
# away from your own saves. ROM overrides the ROM path (default SuperMetroid.sfc).
set -eu

cd "$(dirname "$0")"
ROOT=$(pwd)
ROM=${ROM:-SuperMetroid.sfc}
TARGET=play
KEEP=0
SECTION=explore
MODE=after
BEFORE=${BEFORE:-main}

case "${1:-}" in
  before|after) MODE=$1; shift ;;
esac

while [ $# -gt 0 ]; do
  case "$1" in
    --debug) TARGET=debug; shift ;;
    --dev) TARGET=run; shift ;;
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
case "$ROM" in
  /*) ;;
  *) ROM=$ROOT/$ROM ;;
esac

SRC=$ROOT
DIR=$ROOT/build/playtest/$SECTION
CMAKE_ARGS=
if [ "$MODE" = before ]; then
  SRC=$ROOT/build/before/src
  CMAKE_ARGS=-DSMX_ROM=$ROM
  DIR=$ROOT/build/playtest/before-$SECTION
  git worktree prune
  if [ ! -d "$SRC" ]; then
    git worktree add --detach "$SRC" "$BEFORE"
  else
    git -C "$SRC" checkout --quiet --detach "$BEFORE"
  fi
  # main has no explore section: the same loadout from the landing site.
  if [ "$SECTION" = explore ]; then
    set -- --items all --energy 1499 --reserve 400 --missiles 230 --supers 50 \
      --power-bombs 50 "$@"
    SAVE_SECTION=landing
  else
    SAVE_SECTION=$SECTION
  fi
else
  SAVE_SECTION=$SECTION
fi

make -C "$SRC" build CMAKE_ARGS="$CMAKE_ARGS"
if [ "$KEEP" = 0 ] || [ ! -f "$DIR/$(basename "${ROM%.*}").state" ]; then
  "$SRC/build/debug/smxsave" --section "$SAVE_SECTION" "$@" "$ROM" "$DIR"
fi
echo "playtest: $MODE ($(git -C "$SRC" log -1 --format='%h %s'))"
SMX_DATA_DIR=$DIR exec make -C "$SRC" "$TARGET" ROM="$ROM" CMAKE_ARGS="$CMAKE_ARGS" \
  ARGS=--load-state
