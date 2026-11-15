#!/usr/bin/env python3
"""Checks every symbol named in macOS/Platform/SymbolMappings.xml against an installed game binary.

Run after a game update: a symbol that disappeared or changed signature (its mangled name encodes the
parameter types) shows up here instead of as a crash in game.

    macOS/scripts/check-symbols.py ["/path/to/Baldur's Gate 3.app"]
"""
import os
import re
import subprocess
import sys

DEFAULT_APP = os.path.expanduser(
    "~/Library/Application Support/Steam/steamapps/common/Baldurs Gate 3/Baldur's Gate 3.app")


def main() -> int:
    app = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_APP
    binary = os.path.join(app, "Contents", "MacOS", "Baldur's Gate 3")
    if not os.path.exists(binary):
        sys.exit(f"game binary not found: {binary}")

    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "Platform", "SymbolMappings.xml")) as f:
        xml = f.read()
    xml = re.sub(r"<!--.*?-->", "", xml, flags=re.S)
    wanted = re.findall(r'<Symbol Name="([^"]+)" Mangled="([^"]+)"', xml)

    nm = subprocess.run(["nm", "-arch", "arm64", binary], capture_output=True, text=True, check=True)
    present = set()
    for line in nm.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] != "U":
            present.add(parts[2])

    missing = [(name, mangled) for name, mangled in wanted if "_" + mangled not in present]
    print(f"{len(wanted) - len(missing)}/{len(wanted)} mapped symbols present in {binary}")
    for name, mangled in missing:
        print(f"  MISSING {name}: {mangled}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
