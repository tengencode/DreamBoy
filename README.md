# GBA Emulator for Android

A Game Boy Advance emulator for Android phones. The emulator core is C with hand-written ARM64 assembly and NEON, so it
runs well on cheap and old phones; the app around it is Kotlin and Jetpack Compose.

- Save states (10 slots and a quick slot), rewind, fast-forward, cheats (Action Replay, GameShark, CodeBreaker, raw, and
  libretro `.cht` files)
- Touch controls with a layout editor, 13 console skins, imported Delta skins (`.deltaskin`), keyboards and gamepads
- Tilt, gyro, solar sensor and rumble cartridges; the real-time clock
- RetroAchievements (sign in with your own account): achievements and leaderboards
- Box art from libretro, backup and restore of everything as one `.zip`
- A built-in BIOS; your own BIOS file is optional

**No games or BIOS come with the app.** Use games you own.

## Build

The tools are set up for Windows with Git Bash:

- Android SDK with NDK 27.2.12479018 and CMake 3.31.6 (`ANDROID_HOME`, default `%LOCALAPPDATA%\Android\Sdk`)
- Android Studio's Java (`JBR` in `tools/dev.sh`), for Gradle
- clang from llvm-mingw for the PC builds (`LLVM_MINGW_BIN`)
- Python 3

```bash
bash tools/dev.sh configure     # once: the PC debug build (build/host)
bash tools/dev.sh test          # the PC tests (unit tests, test ROMs, replays)
bash tools/dev.sh apk           # the debug app: app/build/outputs/apk/debug/app-debug.apk
bash tools/dev.sh apk-release   # the release app (signed when keystore.properties names a key)
bash tools/dev.sh jvmtest       # the app's Kotlin tests
bash tools/dev.sh androidtest   # the on-device tests on the phone emulator
```

`tools/dev.sh` lists every command at its top. Test ROMs are downloaded, and checked against pinned SHA-256 sums, with
`python tools/fetch_test_roms.py`. They are never committed.

To check your own games, list them in `tests/games/games.local.txt` and `tests/bench/scenes.local.txt`. Both are
gitignored, and their format is described in `tests/games/games.txt` and `tests/bench/scenes.txt`.

## Layout

- `core/`: the emulator (CPU, memory, graphics, sound, save chips, cartridge hardware, the built-in BIOS, save states,
  cheats), plain C11 with ARM64 fast paths in `core/arch/arm64`, each with a plain-C twin it is tested against
- `platform/android/`: OpenGL ES, AAudio, threads and JNI; `platform/ra/`: the RetroAchievements glue
- `app/`: the Kotlin app
- `tools/`: the PC test runner, viewer, speed bench and helper scripts
- `tests/`: unit tests, the project's own test ROMs and the expected results
- `docs/`: the design and the development notes

## Licence

GPL-3.0-or-later: see [LICENSE](LICENSE). Third-party code is listed in [third_party/README.md](third_party/README.md)
(rcheevos, MIT).

Game Boy Advance is a trademark of Nintendo. This project is not affiliated with or endorsed by Nintendo.
