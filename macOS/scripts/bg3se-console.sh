#!/bin/bash
# Connects to the Script Extender console of a running game (see macOS/README.md).
# Type Lua code or console commands (`help`, `server`, `client`, `reset`); Ctrl-D to leave.
# The game must have been started with the console enabled (BG3SE_CONSOLE=1 or "CreateConsole": true).
sock="$HOME/Library/Application Support/BG3ScriptExtender/console.sock"
[ -S "$sock" ] || { echo "bg3se: no console socket at $sock (is the game running with the console enabled?)" >&2; exit 1; }
exec nc -U "$sock"
