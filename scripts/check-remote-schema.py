#!/usr/bin/env python3
"""Checks that every option in the phone remote's schema (shared/remote/schema.json) is handled by
each app, so the phone page can't drift from the apps: every value id and button id must appear in
the Windows/Linux glue (windows/src/main.cpp) and in the Mac's PhoneRemote.swift."""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
schema = json.loads((ROOT / "shared" / "remote" / "schema.json").read_text())
APPS = {
    "windows": (ROOT / "windows" / "src" / "main.cpp").read_text(),
    "linux": (ROOT / "windows" / "src" / "main.cpp").read_text(),   # linux/src/main.cpp is generated from it
    "mac": (ROOT / "Sources" / "Dropocalypse" / "PhoneRemote.swift").read_text(),
}
KEYS = ("id", "act", "prev", "next", "cycle", "setCycle", "setText", "setFont", "setColor", "portrait",
        "running", "locked", "options")
ids = {}   # id -> platforms that show it


def walk(node, platforms):
    if isinstance(node, list):
        for n in node:
            walk(n, platforms)
        return
    if not isinstance(node, dict):
        return
    if "only" in node:
        platforms = [p for p in platforms if p in node["only"]]
    for k in KEYS:
        if isinstance(node.get(k), str):
            ids.setdefault(node[k], set()).update(platforms)
    for k, v in node.items():
        if k not in KEYS and k != "only":
            walk(v, platforms)


for section in schema["sections"]:   # section ids only name the section
    walk(section["items"], list(APPS))
missing = [f"{i} ({p})" for i, ps in sorted(ids.items()) for p in sorted(ps) if f'"{i}"' not in APPS[p]]
for p, order in schema["order"].items():
    if p not in APPS:
        missing.append(f"unknown platform in order: {p}")
if missing:
    sys.exit("Phone remote options not handled by the app:\n  " + "\n  ".join(missing))
print(f"phone remote schema: {len(ids)} ids handled on every platform")
