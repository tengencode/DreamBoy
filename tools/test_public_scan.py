"""Tests of tools/public_scan.py: python tools/test_public_scan.py"""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import public_scan as ps  # noqa: E402

GAME = "zz_secret_quest | Secret Quest (World).gba | user | - | - | 60:A | 600 | - | - | -\n"


class Scan(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.git("init", "-q")
        self.git("config", "user.email", "t@example.invalid")
        self.git("config", "user.name", "T")
        (self.root / "tests/games").mkdir(parents=True)
        (self.root / "tests/games/games.local.txt").write_text(GAME)  # untracked, as in the real repo
        self.commit("README.md", "hello\n")

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, *args):
        subprocess.run(["git", *args], cwd=self.root, check=True, capture_output=True)

    def commit(self, path, text):
        f = self.root / path
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(text)
        self.git("add", path)
        self.git("commit", "-q", "-m", f"add {path}")

    def test_clean_repo_says_clean(self):
        self.assertEqual(ps.scan(self.root, history=True), [])

    def test_finds_a_local_game_name_in_a_tracked_file(self):
        self.commit("docs/notes.md", "played secret quest today\n")
        hits = ps.scan(self.root)
        self.assertEqual(len(hits), 1)
        self.assertIn("docs/notes.md:1", hits[0])

    def test_finds_a_users_folder_path(self):
        # split, so this file doesn't trip the scanner itself
        self.commit("a.txt", "see C:" + "\\Users\\someone\\Downloads\n")
        self.commit("b.txt", "or /c/" + "Users/someone/x\n")
        self.assertEqual(len(ps.scan(self.root)), 2)

    def test_finds_an_extra_word(self):
        (self.root / "tests/public_scan.local.txt").write_text("# my words\nphone-serial-123\n")
        self.commit("c.txt", "device PHONE-SERIAL-123 attached\n")
        self.assertEqual(len(ps.scan(self.root)), 1)

    def test_skips_third_party(self):
        self.commit("third_party/lib/x.c", "/* Secret Quest support */\n")
        self.assertEqual(ps.scan(self.root), [])

    def test_history_finds_a_word_in_an_old_commit(self):
        self.commit("old.md", "secret quest\n")
        self.git("rm", "-q", "old.md")
        self.git("commit", "-q", "-m", "remove it")
        self.assertEqual(ps.scan(self.root), [])
        self.assertTrue(ps.scan(self.root, history=True))


if __name__ == "__main__":
    unittest.main()
