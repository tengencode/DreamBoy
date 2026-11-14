# DreamBoy

**A fast, free Game Boy, Game Boy Color and Game Boy Advance emulator for Android.**

[![Get it on GitHub](docs/images/get-it-on-github.png)](https://github.com/tengencode/DreamBoy/releases/latest)

| Your games | Playing | Pause menu | Console skins |
|---|---|---|---|
| ![The game list](docs/images/screenshot-1.png) | ![A game with touch controls](docs/images/screenshot-2.png) | ![The pause menu](docs/images/screenshot-3.png) | ![A console skin](docs/images/screenshot-4.png) |

DreamBoy's emulator cores are its own, written in C (the Game Boy Advance one with hand-written ARM64 assembly), so it
runs games at full speed on cheap and old phones. It's free and open source (GPL-3.0), with no ads, no tracking and no
account needed.

## Features

For every game (`.gba`, `.gb`, `.gbc`, or one of them in a `.zip`):

- Save states (10 slots and a quick slot), rewind and fast-forward
- Touch controls with a layout editor, 13 console skins, keyboards and gamepads
- Imported Delta skins (`.deltaskin`) made for Game Boy, Game Boy Color or Game Boy Advance; each console keeps its
  own
- In-game saves in the usual `.sav` format, and the real-time clock of cartridges that have one
- Box art for your game list, and backup and restore of everything as one `.zip`
- Cheats: Game Genie and GameShark for Game Boy and Game Boy Color games; Action Replay, GameShark, CodeBreaker and
  raw codes for Game Boy Advance games; libretro `.cht` files for all
- [RetroAchievements](https://retroachievements.org): achievements and leaderboards (sign in with your own account)
- Rumble and tilt cartridges (tilt the phone); Game Boy Advance games also have the gyro and solar sensors

For grey Game Boy games: colours — the ones a Game Boy Color gives each game, or a look you pick (grey, the original
green, and more), for all games or for one.

Game Boy Advance games use a built-in BIOS (your own BIOS file is optional); Game Boy games need none.

**No games or BIOS come with the app.** Use games you own.

## Install

1. Download `DreamBoy-<version>.apk` from the [latest release](https://github.com/tengencode/DreamBoy/releases/latest)
   on your phone.
2. Open it. If Android asks, allow your browser or files app to install apps.
3. Open DreamBoy and choose the folder where your games are.

DreamBoy needs Android 8 or newer. To get new versions automatically, add this page to
[Obtainium](https://github.com/ImranR98/Obtainium).

## RetroAchievements

Sign in under Settings → RetroAchievements with your own retroachievements.org account. Achievements unlock and
leaderboards can be viewed in games of all three consoles. Hardcore mode, and with it sending leaderboard scores, waits until
RetroAchievements approves DreamBoy, which they consider once an emulator has been public for six months.

## How it was made

DreamBoy was built with a lot of help from an AI assistant (Claude); every commit says so. Its correctness rests on
tests rather than trust: hundreds of unit tests, public CPU, graphics and sound test ROMs for all three consoles, frame
and sound fingerprints, the mGBA test suite, on-device tests, and a check that plays dozens of games by script. Bug
reports are welcome.

## Building

See [docs/BUILDING.md](docs/BUILDING.md). Releasing: [docs/RELEASE.md](docs/RELEASE.md).

## Licence and privacy

GPL-3.0-or-later: see [LICENSE](LICENSE). Third-party code is listed in [third_party/README.md](third_party/README.md)
(rcheevos, MIT); the app lists every part it is built from under Settings → About DreamBoy. What the app does with your
data: [PRIVACY.md](PRIVACY.md).

The screenshots show free homebrew games: Blind Jump by Evan Bowman (MIT), Beat Beast by afska (MIT) and HEXES by
Eigenlight Arts (MIT).

Game Boy, Game Boy Color and Game Boy Advance are trademarks of Nintendo. This project is not affiliated with or endorsed
by Nintendo.
