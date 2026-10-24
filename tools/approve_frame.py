#!/usr/bin/env python3
"""Record an approved frame (and optional audio) fingerprint in tests/expected_frames.json.

usage: approve_frame.py <name> <rom path under tests/roms> <frames> <hash> [<audio hash>]
Only run this after looking at the screenshot (and checking the sound) and confirming it is correct.
"""
import json
import re
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parent.parent / "tests/expected_frames.json"


def main():
    if len(sys.argv) not in (5, 6):
        sys.exit(__doc__)
    name, rom, frames, digest = sys.argv[1:5]
    audio = sys.argv[5] if len(sys.argv) == 6 else None
    if not re.fullmatch(r"[a-z0-9_]+", name):
        sys.exit("name must be lowercase letters, digits and _")
    for value in [digest] + ([audio] if audio else []):
        if not re.fullmatch(r"[0-9a-f]{16}", value):
            sys.exit("hashes must be 16 lowercase hex digits")
    data = json.loads(PATH.read_text())
    entries = [e for e in data["frames"] if e["name"] != name]
    entry = {"name": name, "rom": rom, "frames": int(frames), "hash": digest}
    if audio:
        entry["audio"] = audio
    entries.append(entry)
    data["frames"] = sorted(entries, key=lambda e: e["name"])
    PATH.write_text(json.dumps(data, indent=2) + "\n", newline="\n")
    print(f"approved {name}: {digest}" + (f", audio {audio}" if audio else ""))


if __name__ == "__main__":
    main()
