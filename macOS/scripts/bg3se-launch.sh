#!/bin/bash
# Starts Baldur's Gate 3 (macOS) with the Script Extender loaded.
#
# From Steam: set the game's launch options to
#     /full/path/to/bg3se-launch.sh [options] %command%
# (the launcher must come first: Steam on macOS runs the first word as a program, so the
# `VAR=value %command%` form used on other platforms does not work).
# Directly: run this script, with options only, while Steam is running.
#
# Options:
#   --engine N   engine integration stage (0-3, see macOS/README.md); default 0 = diagnostic only
#   --console    enable the extender console (terminal and console socket)
#   --dry-run    print the command that would be run, and exit
#
# The same settings can be given as environment variables instead (BG3SE_ENGINE, BG3SE_CONSOLE).
# BG3SE_DYLIB overrides the location of libBG3ScriptExtender.dylib (default: next to this script,
# or build/macos in a source checkout).
#
# The extender is injected with DYLD_INSERT_LIBRARIES, which works because the game is not built
# with the hardened runtime. Nothing in the game's bundle is modified.
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
data="$HOME/Library/Application Support/BG3ScriptExtender"
mkdir -p "$data"
log="$data/launcher.log"

# Steam shows nothing of what a launch wrapper prints, so keep a record of each launch
{
  echo "---- $(date '+%Y-%m-%d %H:%M:%S') bg3se-launch"
  i=0; for arg in "$@"; do i=$((i + 1)); echo "arg $i: $arg"; done
} > "$log" 2>&1

fail() {
  echo "bg3se: $*" >&2
  echo "error: $*" >> "$log"
  exit 1
}

dry_run=0
while [ $# -gt 0 ]; do
  case "$1" in
    --engine)   [ $# -ge 2 ] || fail "--engine needs a stage number"; export BG3SE_ENGINE="$2"; shift 2 ;;
    --engine=*) export BG3SE_ENGINE="${1#--engine=}"; shift ;;
    --console)  export BG3SE_CONSOLE=1; shift ;;
    --dry-run)  dry_run=1; shift ;;
    --)         shift; break ;;
    --*)        fail "unknown option: $1" ;;
    *)          break ;;
  esac
done

dylib="${BG3SE_DYLIB:-}"
if [ -z "$dylib" ]; then
  for candidate in "$here/libBG3ScriptExtender.dylib" "$here/../../build/macos/libBG3ScriptExtender.dylib"; do
    if [ -f "$candidate" ]; then dylib="$(cd "$(dirname "$candidate")" && pwd)/$(basename "$candidate")"; break; fi
  done
fi
[ -n "$dylib" ] && [ -f "$dylib" ] || fail "extender library not found (looked next to this script and in build/macos; set BG3SE_DYLIB)"

if [ $# -eq 0 ]; then
  game="$HOME/Library/Application Support/Steam/steamapps/common/Baldurs Gate 3/Baldur's Gate 3.app"
  [ -d "$game" ] || fail "game not found at $game; pass its path as an argument"
  # Started outside Steam, the game would otherwise relaunch itself through Steam (losing the injection)
  export SteamAppId=1086940 SteamGameId=1086940
  set -- "$game"
fi

# Steam may hand over the app bundle rather than the executable inside it
if [ -d "$1" ] && [[ "$1" == *.app || "$1" == *.app/ ]]; then
  bundle="${1%/}"
  executable=$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$bundle/Contents/Info.plist" 2>/dev/null)
  [ -n "$executable" ] && [ -x "$bundle/Contents/MacOS/$executable" ] || fail "no executable found in $bundle"
  shift
  set -- "$bundle/Contents/MacOS/$executable" "$@"
fi
[ -x "$1" ] || fail "not an executable: $1"

{
  echo "dylib: $dylib"
  echo "engine stage: ${BG3SE_ENGINE:-0}  console: ${BG3SE_CONSOLE:-0}"
  echo "game: $1"
} >> "$log"

if [ "$(uname -m)" != "arm64" ]; then
  echo "bg3se: Intel Macs are not supported yet (the hook engine is arm64-only); starting the game without the extender" >&2
  echo "not arm64: starting without the extender" >> "$log"
  [ $dry_run -eq 1 ] && { echo "$@"; exit 0; }
  exec "$@"
fi

# The extender is arm64-only, so make sure the game's arm64 slice is the one that runs. `arch` is
# a protected system binary that drops DYLD_* variables from its own environment; -e hands the
# variable to the game instead.
if [ $dry_run -eq 1 ]; then
  echo "BG3SE_ENGINE=${BG3SE_ENGINE:-0} BG3SE_CONSOLE=${BG3SE_CONSOLE:-0} /usr/bin/arch -arm64 -e DYLD_INSERT_LIBRARIES=$dylib $*"
  exit 0
fi
exec /usr/bin/arch -arm64 -e DYLD_INSERT_LIBRARIES="$dylib" "$@"
