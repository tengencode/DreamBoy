#!/usr/bin/env python3
"""The game check (milestone 13): plays every game in tests/games/games.txt through the test runner.

  game_check.py --runner <testrunner> --games <folder> [--only <name>] [--check | --lock <name|all>]

Without --check / --lock: each game (or --only's) is played with its button script; the run keeps a picture every
second, the sound and the warning log under build/check/work/<name>/, prints a summary, and writes the report
build/check/report/index.html: per game its contact sheet, flags (freeze, blank screen, silence, crash, the game's
own error messages), the log and the game's own messages (mGBA's debug-print registers).
--check plays every listed game without pictures and compares its picture hash, sound hash and log with the ones
locked in the list ("-" = not locked yet); exit 1 when any differs.
--lock plays the named game (or all) the same way and writes its SHA-256, hashes and log into the list.
Games come from --games (source "user"), tests/roms/ (source "test") or tests/roms/homebrew/ (a download URL); a
.zip is unpacked (its one .gba, .gb or .gbc). A save file is copied into build/check/work/ first, so the user's file is never
written. Run it through tools/dev.sh games (which builds build/check and passes --runner and --games).
"""
import argparse
import array
import hashlib
import html
import re
import shutil
import subprocess
import sys
import wave
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIST = ROOT / "tests" / "games" / "games.txt"
LOCAL_LIST = LIST.with_name("games.local.txt")  # the user's own games: gitignored, never published
TEST_ROMS = ROOT / "tests" / "roms"
HOMEBREW = TEST_ROMS / "homebrew"
WORK = ROOT / "build" / "check" / "work"
REPORT = ROOT / "build" / "check" / "report"

FIELDS = ["name", "file", "source", "sha256", "save", "keys", "frames", "picture", "sound", "notes"]
KEY_BITS = {"A": 0, "B": 1, "SELECT": 2, "START": 3, "RIGHT": 4, "LEFT": 5, "UP": 6, "DOWN": 7, "R": 8, "L": 9,
            "CW": 14, "CCW": 15}  # GBA_KEY_* bits; CW / CCW turn the rotation sensor (the test runner's)
CLOCK = 1767268800  # the cartridge clock, 2026-01-01 12:00 UTC (as gbabench), so runs are identical
SHOT_EVERY = 60     # frames between pictures: one second of game time
TAP = 4             # frames a tapped button stays down
SETTLE = 1200       # the first 20 s are not checked for freezes (logos, intros)
FREEZE = 600        # 10 s without a picture change while the buttons change
BLANK = 600         # 10 s of one colour
QUIET = 64          # sound whose peak-to-peak stays below this is silence
CRASH_KINDS = ("exec-empty", "undefined")
ERROR_LEVEL = 1     # the game's own messages at this level or lower (0 fatal, 1 error) are flagged


def parse_list(text):
    """The games of the list: one dict per game line (FIELDS + 'line'); blank and # lines skipped."""
    games = []
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != len(FIELDS):
            raise ValueError(f"line {number}: {len(parts)} fields, expected {len(FIELDS)}")
        game = dict(zip(FIELDS, parts))
        if not re.fullmatch(r"[a-z0-9_]+", game["name"]):
            raise ValueError(f"line {number}: a name may only have a-z, 0-9 and _")
        if not game["frames"].isdigit() or int(game["frames"]) <= 0:
            raise ValueError(f"line {number}: frames must be a positive number")
        if any(g["name"] == game["name"] for g in games):
            raise ValueError(f"line {number}: the name {game['name']} is used twice")
        game["frames"] = int(game["frames"])
        game["line"] = number
        games.append(game)
    return games


def load_lists(paths):
    """The games of every list that exists, each with the 'list' it came from; a name may be used only once."""
    games = []
    for path in paths:
        if not path.exists():
            continue
        for game in parse_list(path.read_text(encoding="utf-8")):
            if any(g["name"] == game["name"] for g in games):
                raise ValueError(f"{path.name} line {game['line']}: the name {game['name']} is used twice")
            games.append(dict(game, list=path))
    return games


def key_mask(keys):
    """'A+B' → GBA_KEY_* bits; '-' → 0."""
    if keys == "-":
        return 0
    mask = 0
    for name in keys.split("+"):
        if name not in KEY_BITS:
            raise ValueError(f"unknown button {name}")
        mask |= 1 << KEY_BITS[name]
    return mask


def expand_keys(script, frames):
    """The buttons down in each frame. Items, comma-separated: 'F:KEYS' holds KEYS from frame F until the next
    such item ('-' = none); 'A-B/N[/D]:KEYS' taps KEYS (down D frames, default TAP) every N frames from A up to B, on top."""
    held, taps, holds = [0] * frames, [0] * frames, []
    if script.strip() != "-":
        for item in (i.strip() for i in script.split(",")):
            when, _, keys = item.partition(":")
            if not keys or not when:
                raise ValueError(f"'{item}': expected FRAME:KEYS or A-B/N:KEYS")
            mask = key_mask(keys)
            if "-" in when:
                span, _, rest = when.partition("/")
                every, _, down = rest.partition("/")
                start, _, end = span.partition("-")
                start, end, every, down = int(start), int(end), int(every or 0), int(down or TAP)
                if every <= 0 or end <= start or down <= 0:
                    raise ValueError(f"'{item}': expected A-B/N[/D]:KEYS with A < B, N > 0 and D > 0")
                for f in range(start, min(end, frames), every):
                    for t in range(f, min(f + down, frames)):
                        taps[t] |= mask
            else:
                holds.append((int(when), mask))
    holds.sort(key=lambda h: h[0])
    for i, (start, mask) in enumerate(holds):
        end = holds[i + 1][0] if i + 1 < len(holds) else frames
        for f in range(start, min(end, frames)):
            held[f] = mask
    return [h | t for h, t in zip(held, taps)]


def key_events(masks):
    """Per-frame masks → the test runner's --keys text: one event wherever the buttons change."""
    events, last = [], 0
    for frame, mask in enumerate(masks):
        if mask != last:
            names = [n for n, bit in KEY_BITS.items() if mask >> bit & 1]
            events.append(f"{frame}:{'+'.join(names) if names else '-'}")
            last = mask
    return ",".join(events) or "0:-"


def same_runs(shots, same):
    """Runs of consecutive pictures that same(a, b) joins, as (first frame, last frame)."""
    runs, i = [], 0
    while i < len(shots):
        j = i
        while j + 1 < len(shots) and same(shots[i], shots[j + 1]):
            j += 1
        runs.append((shots[i][0], shots[j][0]))
        i = j + 1
    return runs


def find_freezes(shots, masks):
    """Unchanged pictures for FREEZE frames or more while the buttons changed in between; only the part after
    SETTLE counts (a run that starts during the opening still counts from SETTLE on).
    shots: (frame, digest, one colour) in frame order; masks: the buttons per frame."""
    found = []
    for a, b in same_runs(shots, lambda x, y: x[1] == y[1]):
        start = max(a, SETTLE)
        if b - start >= FREEZE and len(set(masks[start:b])) > 1:
            found.append((a, b))
    return found


def find_blank(shots):
    """One-colour pictures (any colour) for BLANK frames or more."""
    return [(a, b) for a, b in same_runs(shots, lambda x, y: x[2] and y[2]) if b - a >= BLANK]


def parse_output(text):
    """The test runner's HASH, AUDIO, NOTE and PRINT lines."""
    out = {"picture": None, "sound": None, "notes": [], "prints": []}
    for line in text.splitlines():
        words = line.split()
        if len(words) >= 4 and words[0] == "PRINT":
            parts = line.split(" ", 4)
            out["prints"].append({"level": int(parts[1]), "first": int(parts[2]), "count": int(parts[3]),
                                  "text": parts[4] if len(parts) > 4 else ""})
        elif len(words) == 2 and words[0] == "HASH":
            out["picture"] = words[1]
        elif len(words) == 2 and words[0] == "AUDIO":
            out["sound"] = words[1]
        elif len(words) == 5 and words[0] == "NOTE":
            out["notes"].append({"kind": words[1], "detail": words[2], "first": int(words[3]), "count": int(words[4])})
    return out


def print_flags(prints):
    """Flags for the game's own error messages (level ERROR_LEVEL or lower)."""
    return [f"game error (level {p['level']}): {p['text']} (frame {p['first']}, {p['count']} times)"
            for p in prints if p["level"] <= ERROR_LEVEL]


def notes_summary(notes):
    """The log as the list locks it: sorted kind:detail words, '-' for none."""
    return " ".join(sorted({f"{n['kind']}:{n['detail']}" for n in notes})) or "-"


def folder(game, games_dir):
    if game["source"] == "user":
        return Path(games_dir)
    if game["source"] == "test":
        return TEST_ROMS
    return HOMEBREW


GAME_ENDINGS = (".gba", ".gb", ".gbc")


def game_member(names):
    """The one game file (.gba, .gb or .gbc) among a zip's member names."""
    members = [n for n in names if n.lower().endswith(GAME_ENDINGS) and not n.startswith("__MACOSX/")]
    if len(members) != 1:
        raise ValueError(f"{len(members)} game files inside, expected 1")
    return members[0]


def check_keys(file_name, masks):
    """A Game Boy or Game Boy Color game has no L, R or rotation sensor: a script using them is a mistake."""
    extra = (1 << KEY_BITS["R"]) | (1 << KEY_BITS["L"]) | (1 << KEY_BITS["CW"]) | (1 << KEY_BITS["CCW"])
    if not file_name.lower().endswith(".gba") and any(m & extra for m in masks):
        raise ValueError(f"{file_name}: L, R, CW and CCW are not Game Boy buttons")


def rom_bytes(game, games_dir):
    """The game's bytes and its file name (the member's, for a zip), or None when the file is missing."""
    path = folder(game, games_dir) / game["file"]
    if not path.exists():
        return None
    if path.suffix.lower() != ".zip":
        return path.read_bytes(), path.name
    with zipfile.ZipFile(path) as z:
        try:
            member = game_member(z.namelist())
        except ValueError as e:
            raise ValueError(f"{path.name}: {e}") from None
        return z.read(member), member


def run_text(cmd):
    """Runs a program: (stdout, stderr, exit code). Bytes that are not UTF-8 (a game's own messages may have any)
    become U+FFFD."""
    done = subprocess.run(cmd, capture_output=True, encoding="utf-8", errors="replace")
    return done.stdout, done.stderr, done.returncode


def run_game(game, args, pictures):
    """Plays one game; a result dict, or None when its file is missing."""
    found = rom_bytes(game, args.games)
    if found is None:
        return None
    rom, rom_name = found
    is_gba = rom_name.lower().endswith(".gba")
    rom_file = "game" + Path(rom_name).suffix.lower()
    work = WORK / game["name"]
    if work.exists():
        shutil.rmtree(work)
    (work / "shots").mkdir(parents=True)
    (work / rom_file).write_bytes(rom)
    masks = expand_keys(game["keys"], game["frames"])
    check_keys(rom_name, masks)
    (work / "keys.txt").write_text(key_events(masks))
    cmd = [args.runner, str(work / rom_file), "--frames", str(game["frames"]), "--rtc", str(CLOCK),
           "--keys", "@" + str(work / "keys.txt"), "--hash", "--audio-hash"]
    if is_gba:
        cmd.append("--notes")  # the warning log is the GBA core's
    if game["save"] != "-":
        save = folder(game, args.games) / game["save"]
        if not save.exists():
            raise ValueError(f"{game['name']}: the save file {save} is missing")
        shutil.copyfile(save, work / "game.sav")
        cmd += ["--save", str(work / "game.sav")]
    if pictures:
        cmd += ["--shots-every", str(SHOT_EVERY), "--shots-dir", str(work / "shots"), "--wav", str(work / "sound.wav")]
    stdout, stderr, code = run_text(cmd)
    (work / "output.txt").write_text(stdout + stderr, encoding="utf-8")
    if code != 0:
        raise RuntimeError(f"{game['name']}: the test runner stopped ({code}):\n{stdout}{stderr}")
    result = parse_output(stdout)
    result.update(name=game["name"], sha256=hashlib.sha256(rom).hexdigest(), masks=masks, work=work)
    return result


def peak_to_peak(wav_path):
    with wave.open(str(wav_path)) as w:
        samples = array.array("h", w.readframes(w.getnframes()))
    return max(samples) - min(samples) if samples else 0


def analyse(result):
    """Adds the pictures' digests and the flags."""
    from PIL import Image
    shots = []
    for path in sorted((result["work"] / "shots").glob("*.png")):
        with Image.open(path) as im:
            im = im.convert("RGB")
            one_colour = all(lo == hi for lo, hi in im.getextrema())
            shots.append((int(path.stem), hashlib.sha1(im.tobytes()).hexdigest(), one_colour))
    flags = [f"freeze: no change from frame {a} to {b} while buttons changed" for a, b in find_freezes(shots, result["masks"])]
    flags += [f"blank screen: one colour from frame {a} to {b}" for a, b in find_blank(shots)]
    if peak_to_peak(result["work"] / "sound.wav") < QUIET:
        flags.append("silence: no sound in the whole run")
    flags += [f"crash: {n['kind']} at {n['detail']} (frame {n['first']})" for n in result["notes"] if n["kind"] in CRASH_KINDS]
    flags += print_flags(result["prints"])
    result["flags"] = flags


def contact_sheet(result, out_path):
    """All pictures at half size, 10 per row, each with its frame number."""
    from PIL import Image, ImageDraw
    paths = sorted((result["work"] / "shots").glob("*.png"))
    cols, w, h, label = 10, 120, 80, 12
    rows = max(1, (len(paths) + cols - 1) // cols)
    sheet = Image.new("RGB", (cols * w, rows * (h + label)), "white")
    draw = ImageDraw.Draw(sheet)
    for i, path in enumerate(paths):
        x, y = (i % cols) * w, (i // cols) * (h + label)
        with Image.open(path) as im:
            sheet.paste(im.convert("RGB").resize((w, h)), (x, y))
        draw.text((x + 2, y + h), str(int(path.stem)), fill="black")
    sheet.save(out_path)


def write_report(results, missing):
    REPORT.mkdir(parents=True, exist_ok=True)
    parts = ["<!doctype html><meta charset='utf-8'><title>Game check</title>",
             "<style>body{font-family:sans-serif;margin:16px;max-width:1240px}img{max-width:100%}"
             ".flag{color:#b00020}table{border-collapse:collapse}td,th{border:1px solid #999;padding:2px 6px}</style>",
             "<h1>Game check</h1>"]
    for r in results:
        contact_sheet(r, REPORT / f"{r['name']}.png")
        parts.append(f"<h2>{html.escape(r['name'])}</h2>")
        parts += [f"<p class='flag'>{html.escape(f)}</p>" for f in r["flags"]] or ["<p>No flags.</p>"]
        if r["notes"]:
            rows = "".join(f"<tr><td>{html.escape(n['kind'])}</td><td>{n['detail']}</td><td>{n['first']}</td>"
                           f"<td>{n['count']}</td></tr>" for n in r["notes"])
            parts.append(f"<table><tr><th>Warning</th><th>Detail</th><th>First frame</th><th>Count</th></tr>{rows}</table>")
        else:
            parts.append("<p>Warning log: empty.</p>")
        if r["prints"]:
            rows = "".join(f"<tr><td>{p['level']}</td><td>{p['first']}</td><td>{p['count']}</td>"
                           f"<td>{html.escape(p['text'])}</td></tr>" for p in r["prints"])
            parts.append(f"<table><tr><th>Level</th><th>First frame</th><th>Count</th><th>The game's own message</th>"
                         f"</tr>{rows}</table>")
        parts.append(f"<p>picture {r['picture']}, sound {r['sound']}</p>"
                     f"<img src='{r['name']}.png' alt='{html.escape(r['name'])}: a picture every second'>")
    parts += [f"<h2>{html.escape(name)}</h2><p>skipped (missing)</p>" for name in missing]
    (REPORT / "index.html").write_text("\n".join(parts), encoding="utf-8")


def check(games, args):
    differ = 0
    for game in games:
        r = run_game(game, args, pictures=False)
        if r is None:
            print(f"{game['name']:<16} skipped (missing)")
            continue
        if game["picture"] == "-":
            print(f"{game['name']:<16} not locked yet")
            continue
        wrong = []
        if r["sha256"] != game["sha256"]:
            wrong.append("the game file differs from the listed one")
        if r["picture"] != game["picture"]:
            wrong.append(f"picture {r['picture']}, locked {game['picture']}")
        if r["sound"] != game["sound"]:
            wrong.append(f"sound {r['sound']}, locked {game['sound']}")
        if notes_summary(r["notes"]) != game["notes"]:
            wrong.append(f"warnings '{notes_summary(r['notes'])}', locked '{game['notes']}'")
        print(f"{game['name']:<16} " + ("ok" if not wrong else "DIFFERENT: " + "; ".join(wrong)))
        differ += bool(wrong)
    return 1 if differ else 0


def lock(games, args):
    files = {g["list"]: g["list"].read_text(encoding="utf-8").splitlines() for g in games}
    for game in games:
        r = run_game(game, args, pictures=False)
        if r is None:
            print(f"{game['name']}: skipped (missing), not locked")
            continue
        game.update(sha256=r["sha256"], picture=r["picture"], sound=r["sound"], notes=notes_summary(r["notes"]))
        files[game["list"]][game["line"] - 1] = " | ".join(str(game[f]) for f in FIELDS)
        print(f"{game['name']}: locked picture {r['picture']}, sound {r['sound']}, warnings {game['notes']}")
    for path, lines in files.items():
        path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main():
    p = argparse.ArgumentParser(description="The game check (milestone 13).")
    p.add_argument("--runner", required=True, help="the test runner built with GBA_NOTES (build/check)")
    p.add_argument("--games", required=True, help="the folder with the user's games")
    p.add_argument("--only", help="one game by name")
    mode = p.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="compare with the locked hashes and logs")
    mode.add_argument("--lock", help="lock a game's hashes and log (a name, or all)")
    args = p.parse_args()
    sys.stdout.reconfigure(errors="replace")  # a game's own messages may hold characters the console cannot show
    games = load_lists([LIST, LOCAL_LIST])
    wanted = args.lock if args.lock and args.lock != "all" else args.only
    if wanted:
        games = [g for g in games if g["name"] == wanted]
        if not games:
            sys.exit(f"no game named {wanted} in {LIST.name} or {LOCAL_LIST.name}")
    if args.check:
        sys.exit(check(games, args))
    if args.lock:
        lock(games, args)
        return
    results, missing = [], []
    for game in games:
        r = run_game(game, args, pictures=True)
        if r is None:
            missing.append(game["name"])
            print(f"{game['name']}: skipped (missing)")
            continue
        analyse(r)
        results.append(r)
        print(f"{game['name']}: {len(r['flags'])} flags, {len(r['notes'])} warnings")
        for f in r["flags"]:
            print(f"  {f}")
        for n in r["notes"]:
            print(f"  NOTE {n['kind']} {n['detail']} first frame {n['first']} count {n['count']}")
        for p in r["prints"]:
            print(f"  PRINT level {p['level']} first frame {p['first']} count {p['count']}: {p['text']}")
    write_report(results, missing)
    print(f"report: {REPORT / 'index.html'}")


if __name__ == "__main__":
    main()
