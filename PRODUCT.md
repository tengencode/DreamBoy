# Product

<!-- impeccable:product-schema 1 -->

## Platform

android

## Users
The owner first: a Game Boy Advance player who plays their own ROMs (ROM hacks, commercial games they own, homebrew) on Android phones, mostly with the touch buttons, sometimes with a keyboard or gamepad. Sessions are short and frequent: open the app, tap Continue, play, pause, save a state, quit. Later the app goes public for other GBA players with their own games (confirmed by the user 2026-10-05: "me now, public later"), **free and open source** (user decision 2026-10-23: no paid version, no ads, no in-app purchases).

## Product Purpose
A Game Boy Advance emulator for Android comparable to Pizza Boy GBA: high compatibility, low latency, save states, rewind, fast-forward, cheats, gamepads, sensors (tilt, light, rumble) and customizable touch skins. Success: the user picks up a game exactly where they left it within seconds, and the app feels like a console of its own rather than a stock Android utility.

## Positioning
- The core is written in C with hand-written ARM64 assembly and NEON; it runs games at 3.4–6.4× real speed on a 2018 phone's small core (phone B, measured in milestone 7b), so it suits cheap and old phones.
- Nothing leaves the phone except box-art downloads and, for players who sign in, RetroAchievements: no cloud, no app keys (user decision 2026-10-04). The one sign-in is the player's own retroachievements.org account (user decision 2026-10-05, milestone 14): the login token is kept, never the password. Users keep their saves with the built-in backup `.zip`.
- Imports Delta emulator skins (`.deltaskin`) and libretro `.cht` cheat files; 13 built-in console skins in the colours of special-edition handhelds.

## Operating Context
- Used on a phone held upright (game picture above, touch buttons below) or sideways (picture in the middle, buttons over it); in-game menus sit over a paused game.
- The user's devices: phone A (Android 16) and phone B (Android 11); development on a Pixel 9 emulator (x86_64).
- Players bring their own games folder (Android's folder picker) and, optionally, their own BIOS file.

## Capabilities and Constraints
- Screens: first start, game library (Continue card + grid, box art), game sheet, pause menu, save/load state slots, cheats panel and dialogs, Settings (Picture, Sound, Fast-forward and rewind, Keyboard and controller, Touch buttons + skins + layout editor, BIOS, Backup, box-art switch, About DreamBoy with the open-source licences and the privacy policy), key table, game settings, restore questions, overlays over the game (speed label, light slider).
- Stack: Kotlin + Jetpack Compose (Compose 1.11.4, Material 3 1.4.0, material-icons-extended 1.7.8); Android 8+ (minSdk 26), target 36. Gradle builds offline; any new library or font file needs the user's OK to download.
- The emulator core (C/asm) is out of scope for UI work and must not change.
- No ROMs or BIOS ship with the app; never show or bundle copyrighted game art other than the user's own game pictures and libretro box art downloaded on the phone.
- Free and open source (user decision 2026-10-23) under GPL-3.0-or-later (2026-10-24, `LICENSE`). Released as **DreamBoy** on GitHub only (`https://github.com/tengencode/DreamBoy`, sideloaded; no Play Store) — decided 2026-11-12 (milestone 16). Long-term goal: one app for Game Boy, Game Boy Color and GBA; the first release plays GBA only.

## Brand Commitments
- Plain, short wording: sentence case, buttons start with a verb, no "…", no "&", no "please", no "!" (enforced by `UiTextTest`).
- No integrations with cloud services or third-party sign-ins, except the player's own RetroAchievements account.

## Evidence on Hand
- The user's own game pictures (save-state screenshots) and libretro box art, shown on the device only.
- 13 built-in console skins; imported Delta skins.
- Speed measurements in `docs/PROGRESS.md` (milestones 7a/7b).
- No testimonials, ratings, user counts or store presence exist; never invent them.

## Product Principles
1. Back in the game in one tap: Continue is the front door, menus get out of the way.
2. The game picture is the hero; the app's chrome frames it and never competes with it.
3. Everything stays on the phone and in the user's hands.
4. Fast and light on old phones: no heavy effects that cost frames or battery.
5. Every setting explains itself in plain words where it is changed.

## Accessibility & Inclusion
Touch targets at least 48 dp; text contrast at least 4.5:1; text in sp so it follows the phone's font size; system Back always works.
