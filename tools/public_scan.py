"""Keeps the public repo free of personal details (milestone 16): python tools/public_scan.py [--history]

Looks in every tracked file (and, with --history, every commit and commit message) for
- the user's own games: the file names in the gitignored tests/games/games.local.txt and
  tests/bench/scenes.local.txt,
- a Windows user folder (C:\\Users\\<name>, /c/Users/<name>),
- any word in the gitignored tests/public_scan.local.txt (one per line; # starts a comment).
Case doesn't matter; third_party/ is left out (CLAUDE.md). Prints file:line: word and exits 1 when anything is found.
"""
import re
import subprocess
import sys
from pathlib import Path

LOCAL_LISTS = ["tests/games/games.local.txt", "tests/bench/scenes.local.txt"]
EXTRA = "tests/public_scan.local.txt"
USER_FOLDER = re.compile(r"(?:\b[a-z]:[\\/]+users[\\/]+|/[a-z]/users/)[^\\/\s\"'`<>]+", re.IGNORECASE)
MIN_WORD = 4  # shorter names would match everywhere


def _lines(path):
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return []
    return [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]


def banned_words(root):
    """The words to look for, lower case."""
    words = set()
    for name in LOCAL_LISTS:
        for line in _lines(root / name):
            parts = [p.strip() for p in line.split("|")]
            # The first column is the public stand-in (game_a, hack_1); the file name is the real one.
            if len(parts) > 1:
                stem = Path(parts[1]).stem  # "Some Game (World)" -> "Some Game"
                words.add(re.sub(r"\s*[\(\[][^\)\]]*[\)\]]", "", stem).strip())
    words.update(_lines(root / EXTRA))
    return sorted(w.lower() for w in words if len(w) >= MIN_WORD)


def _hits(text, words):
    low = text.lower()
    found = [w for w in words if w in low]
    found += [m.group(0) for m in USER_FOLDER.finditer(text)]
    return found


def _git(root, *args):
    return subprocess.run(["git", *args], cwd=root, check=True, capture_output=True).stdout


def scan(root, history=False):
    """Every hit as 'where: word'; empty when clean."""
    root = Path(root)
    words = banned_words(root)
    out = []
    for name in _git(root, "ls-files", "-z").decode("utf-8").split("\0"):
        if not name or name.startswith("third_party/"):
            continue
        data = (root / name).read_bytes() if (root / name).is_file() else b""
        if b"\0" in data:  # a picture or other binary file
            continue
        for n, line in enumerate(data.decode("utf-8", "replace").splitlines(), 1):
            out += [f"{name}:{n}: {w}" for w in _hits(line, words)]
    if history:
        current = ""
        log = _git(root, "log", "--all", "-p", "--format=commit %H%n%B").decode("utf-8", "replace")
        for line in log.splitlines():
            if line.startswith("commit "):
                commit = line[7:15]
                current = ""  # a new commit's message belongs to no file
            if line.startswith("diff --git "):
                current = line.split(" b/", 1)[-1]
            if current.startswith("third_party/") and not line.startswith("commit "):
                continue
            out += [f"history {commit} {current}: {w}" for w in _hits(line, words)]
    return out


def main():
    hits = scan(Path.cwd(), history="--history" in sys.argv[1:])
    for h in hits:
        print(h)
    print("clean" if not hits else f"{len(hits)} found")
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())
