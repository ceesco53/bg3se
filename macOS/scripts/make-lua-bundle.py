#!/usr/bin/env python3
"""Packs the builtin Lua scripts into the bundle format read by Lua/Shared/LuaBundle.cpp.

Equivalent of the Windows ResourceBundler tool: a sequence of
    uint32 nameSize, uint32 fileSize, name bytes, file bytes
"""
import os
import struct
import sys


def main(root: str, out_path: str) -> None:
    root = os.path.realpath(root)
    entries = []
    for directory, _, files in os.walk(root):
        for name in files:
            path = os.path.join(directory, name)
            entries.append((os.path.relpath(path, root).replace(os.sep, "/"), path))

    with open(out_path, "wb") as out:
        for bundle_path, path in sorted(entries):
            with open(path, "rb") as f:
                data = f.read()
            encoded = bundle_path.encode("utf-8")
            out.write(struct.pack("<II", len(encoded), len(data)))
            out.write(encoded)
            out.write(data)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: make-lua-bundle.py <LuaScripts dir> <output bundle>")
    main(sys.argv[1], sys.argv[2])
