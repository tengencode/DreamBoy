#!/usr/bin/env python3
"""Download the test ROMs used by CTest into tests/roms/ (gitignored).

- jsmolka/gba-tests: pinned commit, sizes checked.
- Tonc demos: tonc-bin.zip from coranac.com; only the demos listed in TONC_DEMOS are kept.
- mGBA test suite: suite-latest.zip from mGBA's storage, SHA-256 pinned; suite.gba is kept.
- Homebrew games for the game check: pinned URLs, sizes and SHA-256s (HOMEBREW), in tests/roms/homebrew/.
- Game Boy / Game Boy Color test ROMs (milestone 17), in tests/roms/gb/: blargg's test ROMs (a pinned commit of the
  retrio/gb-test-roms collection), the mooneye test suite's prebuilt package, dmg-acid2 and cgb-acid2 with their
  reference pictures. Every one is checked against a pinned SHA-256.
Files that are already present with the right size are not downloaded again.
"""
import hashlib
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "tests/roms"
JSMOLKA_COMMIT = "a7113b67e63f83a9b321696ddd7042ccfad6c881"
JSMOLKA = {  # local name: (path in the repository, size in bytes)
    "arm.gba": ("arm/arm.gba", 8824),
    "thumb.gba": ("thumb/thumb.gba", 3680),
    "memory.gba": ("memory/memory.gba", 2172),
    "bios.gba": ("bios/bios.gba", 1908),
    "hello.gba": ("ppu/hello.gba", 1300),
    "shades.gba": ("ppu/shades.gba", 352),
    "stripes.gba": ("ppu/stripes.gba", 324),
    "sram.gba": ("save/sram.gba", 2084),
    "flash64.gba": ("save/flash64.gba", 3708),
    "flash128.gba": ("save/flash128.gba", 4096),
    "none.gba": ("save/none.gba", 1628),
}
TONC_URL = "http://www.coranac.com/files/tonc-bin.zip"  # the site's HTTPS resets connections; the hash below guards the file
TONC_SIZE = 958346
TONC_SHA256 = "f172fc9308eb030ae8d3479872e9f3521b7f0cae3a64b0cd5f526b7b4b1650ba"  # v1.4.2, 2013-03-24
TONC_DEMOS = [
    "first", "m3_demo", "bm_modes", "pageflip", "obj_demo", "obj_aff", "oacombo", "sbb_reg", "cbb_demo",
    "brin_demo", "sbb_aff", "bld_demo", "win_demo", "mos_demo", "dma_demo", "tmr_demo", "irq_demo",
    "m7_demo", "swi_demo", "snd1_demo",
]
MGBA_SUITE_URL = "https://s3.amazonaws.com/mgba/suite-latest.zip"
MGBA_SUITE_SIZE = 88220
MGBA_SUITE_SHA256 = "b802bb1a08566456f1820d09f8c5362ede30c838297fd05acdf862f1b1e790a5"  # downloaded 2026-09-25
MGBA_SUITE_ROM_SIZE = 524288

# Homebrew games for the game check (milestone 13), downloaded with the user's OK on 2026-10-05; free downloads
# from their authors' GitHub releases, for local testing only (never committed). Each was downloaded twice and
# the copies matched before pinning. local file name (in tests/roms/homebrew/): (URL, size in bytes, SHA-256)
HOMEBREW = {
    "BlindJump.gba": (
        "https://github.com/evanbowman/blind-jump-portable/releases/download/2021.11.20.0/BlindJump.gba",
        16565008, "597d11e93567f14f97ba4bc44bf41691b5e0fbac8dd169e6b559bb9086a1739d"),
    "Skyland.gba": (
        "https://github.com/evanbowman/skyland-gba/releases/download/2022.1.7.0/Skyland.gba",
        3041425, "516f185f206366f9d44ed2df0d8afb2bc0e2f7d3adc2a2ed6e686c6c3eb4ac90"),
    "Google-Dino-Advance.1.1.zip": (
        "https://github.com/fritzem/Google-Dino-GBA/releases/download/1.1/Google-Dino-Advance.1.1.zip",
        50684, "e3a6571294fac02a8585ca53367c8b0b85352277286fc6d61cdc4cd8ede3ca5e"),
    "sym_merged_Jam_Demo.zip": (
        "https://github.com/copyrat90/sym_merged/releases/download/Jam-Demo/sym_merged_Jam_Demo.zip",
        1191228, "b9f670045ad416191431489f0326f04f4560e8eb79838d4d045579b93d0bd628"),
    "voxelburg_jam.zip": (
        "https://github.com/MutantStargoat/voxelburg/releases/download/jam/voxelburg_jam.zip",
        1189517, "e31811acda504a410b10d4824c0f2fc2e48b09c3383348994a9571d89e397b35"),
    "HEXES_jam.gba": (
        "https://github.com/EigenlightArts/HEXES/releases/download/v0.1.1-gbajam22/HEXES_jam.gba",
        4861804, "a308c3e99e6aca1653e17bc7efb8a656e2907033d14845f83ba78d30075be6ad"),
    "Advancetris-0.1.gba": (
        "https://github.com/zniqco/Advancetris/releases/download/0.1/Advancetris-0.1.gba",
        553516, "ba35cc6cbda919d77dd406c0037428036367711050afa464266043a0028f6b94"),
    "Pentris.gba": (
        "https://github.com/andrewbailey/Pentris/releases/download/v1.0/Pentris.gba",
        1161944, "01063cc74939e3cf14c938b89c5c6c685586201b8a376d494ec5ea735bdc8d97"),
    "A-4D-Christmas-0-3.gba": (
        "https://github.com/NotImplementedLife/A-4D-Christmas/releases/download/v0.3/A-4D-Christmas-0-3.gba",
        443980, "d0cd626f2838d7bf43f42ce685c2d0ae67a2cffe0fa173cd45f9950818995953"),
    "Celeste.Classic.v1.2.Homebrew.gba": (
        "https://github.com/JeffRuLz/Celeste-Classic-GBA/releases/download/v1.2/Celeste.Classic.v1.2.Homebrew.gba",
        5418424, "4463620519f9c60a435ef0a6fa2cca123409380881fb0d00c63f2daed0c4f85b"),
    "minicraft-gba_2.0.zip": (
        "https://github.com/Vulcalien/minicraft-gba/releases/download/2.0/minicraft-gba_2.0.zip",
        2041444, "becfd00be700403d61257130322b1798251b3a2de17ba97bf4b14a9390eb34d7"),
    "BeatBeast_jam.gba": (
        "https://github.com/afska/beat-beast/releases/download/v1.1.7/BeatBeast_jam.gba",
        33506864, "2a4986bdf6bd65601f1ae08f471d0aafc3e23d57f594162f77f351a1150e43eb"),
    "dungeon_master.gba": (
        "https://github.com/Maksasj/dungeon_master/releases/download/0.2/main.gba",
        872004, "0ad1222343cc71a27ec22f7e2b78338d90f9b986652be7d857732e5387fb8eb8"),
    "wigly_hunt_-_LD48.gba": (
        "https://github.com/rawbits2010/Wigly-Hunt/releases/download/v0.1/wigly_hunt_-_LD48.gba",
        57692, "f3c4c6bd0d4134d6149602554679d2329a52c885b2822eb4656554734e0fa213"),
    "NotebookAdventure-1-3.gba": (
        "https://github.com/NotImplementedLife/NotebookAdventure/releases/download/v1.3/NotebookAdventure-1-3.gba",
        476392, "944ed052a38f7d01df79bae760f102e5c19527471fc834c8986cb19606fb7e9a"),
    "Healz_jam.gba": (
        "https://github.com/jaswged/Healz_gba_jam/releases/download/v1.0/Healz_jam.gba",
        10800880, "3c129997d50c2ece30f15303fa995944cadde23717c311bcb2e19e6d46eb83c4"),
    "Solar_Guard_GBA_JAM_2021.gba": (
        "https://github.com/Deft-Spade/Solar-Guard/releases/download/GBA-JAM-2021/Solar_Guard_GBA_JAM_2021.gba",
        4272776, "cfd07990e0fa05611fb36b3230d68cea4b65ae11482e822b5b6f97a32a8b88b0"),
    "noonlight_paradox_gba.zip": (
        "https://github.com/zegalur/noonlight-paradox-gba/releases/download/v1.0.0/noonlight_paradox_gba.zip",
        357700, "680cddf9f737cc9fc3dbafa607b67b787dea7cf9040b753ad71cf0373250477b"),
    "chiptrack-0.5.gba": (
        "https://github.com/jturcotte/chiptrack/releases/download/v0.5/chiptrack-0.5.gba",
        444352, "15d7ba294d2c53d605a8ccaa0a9024e00fa5bd9532c273e0e08eae3ce874bd61"),
}


# Game Boy / Game Boy Color test ROMs, downloaded with the user's OK on 2026-11-12 (never committed). Each was
# downloaded twice and the copies matched before pinning.
BLARGG_COMMIT = "c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15"  # github.com/retrio/gb-test-roms
BLARGG_ROMS = 58  # .gb files in it
BLARGG_SHA256 = "bb4d10f439b35800c5cad254f8b653b2240149674a9e14ee9d632bf1c134ccce"  # of the sorted "<path> <sha256 of the file>" lines of those ROMs (GitHub's zip itself may change)
MOONEYE = "mts-20240926-1737-443f6e1"  # gekkio.fi/files/mooneye-test-suite
MOONEYE_SHA256 = "5cf50314cd3d42ec8f423b0a71b9ab6dbfad2386d8d88f03687af1b5b5baba71"  # of the .zip
GB_ACID = {  # local name in tests/roms/gb/acid/: (URL, size in bytes, SHA-256)
    "dmg-acid2.gb": ("https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb", 32768,
                     "464e14b7d42e7feea0b7ede42be7071dc88913f75b9ffa444299424b63d1dff1"),
    "dmg-acid2.png": ("https://raw.githubusercontent.com/mattcurrie/dmg-acid2/v1.0/img/reference-dmg.png", 787,
                      "ca966d50895c7efef05838590d148c2cbfd7fba57dab986f25b35b4da71abb57"),
    "cgb-acid2.gbc": ("https://github.com/mattcurrie/cgb-acid2/releases/download/v1.1/cgb-acid2.gbc", 32768,
                      "197fb0bcec544f0400527fc707e0a94f55435974986e6986b424ace5de81720e"),
    "cgb-acid2.png": ("https://raw.githubusercontent.com/mattcurrie/cgb-acid2/v1.1/img/reference.png", 870,
                      "9ea9c262c5383353e77d715d021a0f7c5ccbe438f88082cb225756e50c4fdf01"),
}


def fetch(url):
    # coranac.com drops requests carrying Python's default User-Agent.
    request = urllib.request.Request(url, headers={"User-Agent": "gba-emulator-fetch/1.0"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def fetch_jsmolka():
    out_dir = ROOT / "jsmolka"
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, (path, size) in JSMOLKA.items():
        out = out_dir / name
        if out.exists() and out.stat().st_size == size:
            continue
        data = fetch(f"https://raw.githubusercontent.com/jsmolka/gba-tests/{JSMOLKA_COMMIT}/{path}")
        if len(data) != size:
            sys.exit(f"{name}: expected {size} bytes, got {len(data)}")
        out.write_bytes(data)
        print(f"jsmolka/{name}: {len(data)} bytes")


def fetch_tonc():
    out_dir = ROOT / "tonc"
    out_dir.mkdir(parents=True, exist_ok=True)
    if all((out_dir / f"{d}.gba").exists() for d in TONC_DEMOS):
        return
    data = fetch(TONC_URL)
    if len(data) != TONC_SIZE:
        sys.exit(f"tonc-bin.zip: expected {TONC_SIZE} bytes, got {len(data)}")
    if hashlib.sha256(data).hexdigest() != TONC_SHA256:
        sys.exit("tonc-bin.zip: SHA-256 mismatch (the download is plain HTTP; not using it)")
    found = {}
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        roms = [n for n in z.namelist() if n.lower().endswith(".gba")]
        for entry in roms:
            stem = entry.rsplit("/", 1)[-1][:-4]
            if stem.endswith(".mb"):
                stem = stem[:-3]
            if stem in TONC_DEMOS:
                found[stem] = z.read(entry)
    missing = [d for d in TONC_DEMOS if d not in found]
    if missing:
        print("ROMs in the archive:", ", ".join(sorted(roms)))
        sys.exit(f"missing Tonc demos: {', '.join(missing)}")
    for stem in TONC_DEMOS:
        (out_dir / f"{stem}.gba").write_bytes(found[stem])
        print(f"tonc/{stem}.gba: {len(found[stem])} bytes")


def fetch_mgba_suite():
    out = ROOT / "mgba" / "suite.gba"
    if out.exists() and out.stat().st_size == MGBA_SUITE_ROM_SIZE:
        return
    data = fetch(MGBA_SUITE_URL)
    if len(data) != MGBA_SUITE_SIZE or hashlib.sha256(data).hexdigest() != MGBA_SUITE_SHA256:
        sys.exit("suite-latest.zip: size or SHA-256 differs from the pinned file (mGBA replaced it?); not using it")
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        rom = z.read("suite.gba")
    if len(rom) != MGBA_SUITE_ROM_SIZE:
        sys.exit(f"suite.gba: expected {MGBA_SUITE_ROM_SIZE} bytes, got {len(rom)}")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(rom)
    print(f"mgba/suite.gba: {len(rom)} bytes")


def fetch_homebrew():
    out_dir = ROOT / "homebrew"
    out_dir.mkdir(parents=True, exist_ok=True)
    failed = []
    for name, (url, size, sha256) in HOMEBREW.items():
        out = out_dir / name
        if out.exists() and out.stat().st_size == size:
            continue
        try:
            data = fetch(url)
        except OSError as e:
            failed.append(f"{name} ({e})")
            continue
        if len(data) != size or hashlib.sha256(data).hexdigest() != sha256:
            failed.append(f"{name} (size or SHA-256 differs from the pinned file)")
            continue
        out.write_bytes(data)
        print(f"homebrew/{name}: {len(data)} bytes")
    if failed:
        sys.exit("homebrew downloads failed: " + ", ".join(failed))


def check_pin(what, got, pinned):
    if got != pinned:
        sys.exit(f"{what}: SHA-256 {got} differs from the pinned {pinned or '(none yet)'}; not using it")


def fetch_gb_blargg():
    out_dir = ROOT / "gb" / "blargg"
    if (out_dir / ".complete").exists():
        return
    data = fetch(f"https://github.com/retrio/gb-test-roms/archive/{BLARGG_COMMIT}.zip")
    roms = {}
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for entry in z.namelist():
            if entry.lower().endswith(".gb"):
                roms[entry.split("/", 1)[1]] = z.read(entry)  # without the archive's top folder
    lines = "".join(f"{path} {hashlib.sha256(roms[path]).hexdigest()}" + chr(10) for path in sorted(roms))
    if len(roms) != BLARGG_ROMS:
        sys.exit(f"blargg: expected {BLARGG_ROMS} ROMs, found {len(roms)}")
    check_pin("blargg's test ROMs", hashlib.sha256(lines.encode()).hexdigest(), BLARGG_SHA256)
    for path, rom in roms.items():
        if ".." in path.split("/"):
            sys.exit(f"blargg: unsafe path {path}")
        out = out_dir / path
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(rom)
    (out_dir / ".complete").write_text(BLARGG_COMMIT)
    print(f"gb/blargg: {len(roms)} ROMs")


def fetch_gb_mooneye():
    out_dir = ROOT / "gb" / "mooneye"
    if (out_dir / ".complete").exists():
        return
    data = fetch(f"https://gekkio.fi/files/mooneye-test-suite/{MOONEYE}/{MOONEYE}.zip")
    check_pin(f"{MOONEYE}.zip", hashlib.sha256(data).hexdigest(), MOONEYE_SHA256)
    count = 0
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for entry in z.namelist():
            if not entry.lower().endswith(".gb"):
                continue
            path = entry.split("/", 1)[1]
            if ".." in path.split("/"):
                sys.exit(f"mooneye: unsafe path {path}")
            out = out_dir / path
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(z.read(entry))
            count += 1
    (out_dir / ".complete").write_text(MOONEYE)
    print(f"gb/mooneye: {count} ROMs")


def fetch_gb_acid():
    out_dir = ROOT / "gb" / "acid"
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, (url, size, sha256) in GB_ACID.items():
        out = out_dir / name
        if out.exists() and hashlib.sha256(out.read_bytes()).hexdigest() == sha256:
            continue
        data = fetch(url)
        if size and len(data) != size:
            sys.exit(f"{name}: expected {size} bytes, got {len(data)}")
        check_pin(name, hashlib.sha256(data).hexdigest(), sha256)
        out.write_bytes(data)
        print(f"gb/acid/{name}: {len(data)} bytes")


def main():
    fetch_gb_acid()
    fetch_gb_blargg()
    fetch_gb_mooneye()
    fetch_jsmolka()
    fetch_mgba_suite()
    fetch_homebrew()
    try:
        fetch_tonc()
    except OSError as e:  # coranac.com sometimes resets every connection; the jsmolka ROMs are still usable
        sys.exit(f"Tonc download failed ({e}); the jsmolka ROMs are ready, run this again later for Tonc")
    print("test ROMs ready")


if __name__ == "__main__":
    main()
