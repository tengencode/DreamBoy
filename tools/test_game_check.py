"""Tests of tools/game_check.py's pure parts: python tools/test_game_check.py"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_check as gc  # noqa: E402

LINE = "m7 | tonc/m7_demo.gba | test | - | - | 60:UP | 1800 | - | - | -"


class ParseList(unittest.TestCase):
    def test_reads_games_and_skips_comments(self):
        games = gc.parse_list(f"# header\n\n{LINE}\n")
        self.assertEqual(len(games), 1)
        self.assertEqual(games[0]["name"], "m7")
        self.assertEqual(games[0]["frames"], 1800)
        self.assertEqual(games[0]["line"], 3)

    def test_refuses_bad_lines(self):
        for bad in ["m7 | x.gba | test", LINE.replace("1800", "0"), LINE.replace("m7 ", "M 7 "),
                    f"{LINE}\n{LINE}"]:
            with self.assertRaises(ValueError):
                gc.parse_list(bad)


class Lists(unittest.TestCase):
    def test_reads_the_public_list_and_the_users_own_and_remembers_each_games_file(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            public, own = Path(d) / "games.txt", Path(d) / "games.local.txt"
            public.write_text(LINE + "\n", encoding="utf-8")
            games = gc.load_lists([public, own])  # no local list: just the public one
            self.assertEqual([g["name"] for g in games], ["m7"])
            own.write_text("# mine\n" + LINE.replace("m7 ", "mine "), encoding="utf-8")
            games = gc.load_lists([public, own])
            self.assertEqual([(g["name"], g["list"], g["line"]) for g in games], [("m7", public, 1), ("mine", own, 2)])
            own.write_text(LINE, encoding="utf-8")  # the same name in both lists
            with self.assertRaises(ValueError):
                gc.load_lists([public, own])


class Keys(unittest.TestCase):
    def test_holds_last_until_the_next_hold(self):
        masks = gc.expand_keys("10:A,20:-", 30)
        self.assertEqual(masks[9], 0)
        self.assertEqual(masks[10], 1)
        self.assertEqual(masks[19], 1)
        self.assertEqual(masks[20], 0)

    def test_taps_repeat_on_top_of_holds(self):
        masks = gc.expand_keys("0:B,0-20/10:START", 30)
        start, b = 1 << 3, 1 << 1
        self.assertEqual(masks[0], b | start)
        self.assertEqual(masks[3], b | start)
        self.assertEqual(masks[4], b)
        self.assertEqual(masks[10], b | start)
        self.assertEqual(masks[20], b)

    def test_a_tap_can_last_longer(self):
        masks = gc.expand_keys("0-40/20/10:CW", 40)
        self.assertEqual(masks[9], 1 << 14)
        self.assertEqual(masks[10], 0)
        self.assertEqual(masks[29], 1 << 14)
        self.assertEqual(masks[30], 0)

    def test_refuses_unknown_buttons_and_bad_ranges(self):
        for bad in ["10:X", "20-10/5:A", "0-10/0:A", "10", "0-10/5/0:A"]:
            with self.assertRaises(ValueError):
                gc.expand_keys(bad, 30)

    def test_events_mark_each_change(self):
        self.assertEqual(gc.key_events([0, 0, 1, 1 | 64, 0]), "2:A,3:A+UP,4:-")
        self.assertEqual(gc.key_events([0, 0]), "0:-")

    def test_the_rotation_sensor_has_its_own_buttons(self):
        masks = gc.expand_keys("0:CW+A,2:CCW", 3)
        self.assertEqual(masks, [1 | 1 << 14, 1 | 1 << 14, 1 << 15])
        self.assertEqual(gc.key_events(masks), "0:A+CW,2:CCW")


def shots(digests, every=60):
    """(frame, digest, one colour) pictures every `every` frames; digest 'k' means one colour."""
    return [((i + 1) * every, d, d == "k") for i, d in enumerate(digests)]


class Flags(unittest.TestCase):
    def test_a_long_unchanged_picture_while_buttons_change_is_a_freeze(self):
        pics = shots(["a"] * 20 + ["f"] * 15 + ["b"] * 5)  # f from frame 1260 to 2100
        masks = [0] * 2400
        masks[1500] = 1
        self.assertEqual(gc.find_freezes(pics, masks), [(1260, 2100)])

    def test_no_freeze_without_button_changes_or_while_settling_or_when_short(self):
        self.assertEqual(gc.find_freezes(shots(["a"] * 20 + ["f"] * 15 + ["b"] * 5), [0] * 2400), [])
        masks = [0] * 2400
        masks[500] = 1
        self.assertEqual(gc.find_freezes(shots(["f"] * 15 + ["b"] * 25), masks), [])
        masks[1300] = 1
        self.assertEqual(gc.find_freezes(shots(["a"] * 20 + ["f"] * 5 + ["b"] * 15), masks), [])

    def test_a_freeze_that_starts_while_settling_counts_from_the_end_of_settling(self):
        pics = shots(["a"] * 2 + ["f"] * 30)  # f from frame 180 to 1920
        masks = [0] * 2400
        masks[1500] = 1
        self.assertEqual(gc.find_freezes(pics, masks), [(180, 1920)])
        masks = [0] * 2400
        masks[600] = 1  # buttons changed only while settling
        self.assertEqual(gc.find_freezes(pics, masks), [])

    def test_one_colour_for_ten_seconds_is_a_blank_screen(self):
        self.assertEqual(gc.find_blank(shots(["a"] * 3 + ["k"] * 12 + ["b"])), [(240, 900)])
        self.assertEqual(gc.find_blank(shots(["a"] * 3 + ["k"] * 5 + ["b"])), [])


class Runner(unittest.TestCase):
    def test_output_that_is_not_valid_text_is_read_anyway(self):
        text = gc.run_text([sys.executable, "-c", "import sys; sys.stdout.buffer.write(b'PRINT 3 1 1 a\\xf7b\\n')"])
        self.assertEqual(text[0].strip(), "PRINT 3 1 1 a�b")


class Output(unittest.TestCase):
    def test_reads_hashes_and_notes(self):
        out = gc.parse_output("HASH 0123456789abcdef\nAUDIO fedcba9876543210\n"
                              "NOTE bios-call 0000001f 12 3\nNOTE io-unused 000000e0 0 1\n")
        self.assertEqual(out["picture"], "0123456789abcdef")
        self.assertEqual(out["sound"], "fedcba9876543210")
        self.assertEqual(out["notes"][0], {"kind": "bios-call", "detail": "0000001f", "first": 12, "count": 3})
        self.assertEqual(gc.notes_summary(out["notes"]), "bios-call:0000001f io-unused:000000e0")
        self.assertEqual(gc.notes_summary([]), "-")

    def test_reads_the_games_own_messages_and_flags_errors(self):
        out = gc.parse_output("PRINT 1 7229 2 ASSERTION FAILED  FILE=[src/malloc.c]\nPRINT 4 204 1 hello\n")
        self.assertEqual(out["prints"][0], {"level": 1, "first": 7229, "count": 2,
                                            "text": "ASSERTION FAILED  FILE=[src/malloc.c]"})
        self.assertEqual(gc.print_flags(out["prints"]),
                         ["game error (level 1): ASSERTION FAILED  FILE=[src/malloc.c] (frame 7229, 2 times)"])


if __name__ == "__main__":
    unittest.main()
