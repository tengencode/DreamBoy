"""Fetches rcheevos (RetroAchievements' client library, MIT) into third_party/rcheevos: LICENSE, include/ and src/.

The files are committed (milestone 14); run this to fetch them again or, with --check, to compare the committed files
with the release. The download is checked against a pinned SHA-256.
Usage: python tools/fetch_rcheevos.py [--check]
"""
import argparse
import hashlib
import io
import os
import shutil
import sys
import tarfile
import urllib.request

VERSION = "12.5.0"
URL = f"https://github.com/RetroAchievements/rcheevos/archive/refs/tags/v{VERSION}.tar.gz"
SHA256 = "e6df83de4e18f0a19206e711e1e589624dcfcf6bf529c14f6dd2bb2d1ced983f"
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEST = os.path.join(ROOT, "third_party", "rcheevos")


def download():
    req = urllib.request.Request(URL, headers={"User-Agent": "gba-emulator-fetch"})
    with urllib.request.urlopen(req, timeout=60) as r:
        data = r.read()
    got = hashlib.sha256(data).hexdigest()
    if got != SHA256:
        sys.exit(f"rcheevos {VERSION}: SHA-256 {got} is not the pinned {SHA256}")
    return data


def kept_files(data):
    """{path inside rcheevos/: bytes} for LICENSE, include/ and src/."""
    out = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        for m in tar.getmembers():
            if not m.isfile() or "/" not in m.name:
                continue
            rel = m.name.split("/", 1)[1]
            if rel == "LICENSE" or rel.startswith(("include/", "src/")):
                out[rel] = tar.extractfile(m).read()
    return out


def lf(b):
    return b.replace(b"\r\n", b"\n")


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--check", action="store_true", help="compare with the committed files; change nothing")
    args = p.parse_args()
    files = kept_files(download())
    if args.check:
        bad = []
        for rel, b in sorted(files.items()):
            path = os.path.join(DEST, rel)
            if not os.path.isfile(path) or lf(open(path, "rb").read()) != lf(b):
                bad.append(rel)
        for rel in bad:
            print(f"differs: {rel}")
        print(f"rcheevos {VERSION}: {len(files) - len(bad)} of {len(files)} files match")
        sys.exit(1 if bad else 0)
    shutil.rmtree(DEST, ignore_errors=True)
    for rel, b in files.items():
        path = os.path.join(DEST, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(b)
    print(f"rcheevos {VERSION}: {len(files)} files in third_party/rcheevos")


if __name__ == "__main__":
    main()
