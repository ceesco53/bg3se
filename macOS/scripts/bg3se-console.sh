#!/bin/bash
# Console of a running game with the Script Extender loaded (see macOS/README.md).
#
#   bg3se-console.sh                     interactive: type Lua code or console commands
#                                        (`help`, `server`, `client`, `reset`); Ctrl-D to leave
#   bg3se-console.sh 'Lua code'          run one command on the server side and print its output
#   bg3se-console.sh --client 'Lua code' the same, on the client side
#
# The game must have been started with the console enabled (--console, or "CreateConsole": true).
sock="$HOME/Library/Application Support/BG3ScriptExtender/console.sock"
[ -S "$sock" ] || { echo "bg3se: no console socket at $sock (is the game running with the console enabled?)" >&2; exit 1; }

if [ $# -eq 0 ]; then
  exec nc -U "$sock"
fi

context=server
if [ "$1" = "--client" ]; then context=client; shift; fi
[ $# -ge 1 ] || { echo "usage: bg3se-console.sh [--client] 'Lua code'" >&2; exit 2; }

# Commands run on the game's own thread; give the reply a moment to come back
{ printf '%s\n%s\n' "$context" "$*"; sleep 2; } | nc -U "$sock" | sed '1{/^Switching to .* context\.$/d;}'
