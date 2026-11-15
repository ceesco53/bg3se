#!/bin/bash
# Starts Baldur's Gate 3 (macOS) with the Script Extender loaded.
#
# From Steam (recommended): set the game's launch options to
#     "/full/path/to/bg3se-launch.sh" %command%
# Directly: run this script with no arguments while Steam is running.
#
# The extender is injected with DYLD_INSERT_LIBRARIES, which works because the game is not built
# with the hardened runtime. Nothing in the game's bundle is modified.
#
# Environment:
#   BG3SE_CONSOLE=1   print the extender console to this terminal (and accept Lua input)
#   BG3SE_ENGINE=1    enable the experimental engine hooks (default: diagnostic mode)
#   BG3SE_DYLIB       path of libBG3ScriptExtender.dylib (default: next to this script)
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
dylib="${BG3SE_DYLIB:-$here/libBG3ScriptExtender.dylib}"
[ -f "$dylib" ] || { echo "bg3se: extender library not found at $dylib" >&2; exit 1; }

if [ $# -eq 0 ]; then
  game="$HOME/Library/Application Support/Steam/steamapps/common/Baldurs Gate 3/Baldur's Gate 3.app/Contents/MacOS/Baldur's Gate 3"
  [ -x "$game" ] || { echo "bg3se: game not found at $game; pass its path as an argument" >&2; exit 1; }
  # Started outside Steam, the game would otherwise relaunch itself through Steam (losing the injection)
  export SteamAppId=1086940 SteamGameId=1086940
  set -- "$game"
fi

# The extender is arm64-only, so make sure the game's arm64 slice is the one that runs. `arch` is
# a protected system binary that drops DYLD_* variables from its own environment; -e hands the
# variable to the game instead.
if [ "$(uname -m)" = "arm64" ]; then
  exec /usr/bin/arch -arm64 -e DYLD_INSERT_LIBRARIES="$dylib" "$@"
fi

echo "bg3se: Intel Macs are not supported yet (the hook engine is arm64-only); starting the game without the extender" >&2
exec "$@"
