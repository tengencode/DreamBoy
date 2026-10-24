# Milestone 14 — RetroAchievements — Design

**Date:** 2026-10-06
**Status:** Implemented — milestone 14 complete 2026-10-06 (tag `milestone-14`): design approved by the user in brainstorming 2026-10-06, the written spec approved ("spec approved"), refined while planning (§1: the hardcore limits only for games with a set; §2: the game identified before it starts; §3–4: what rcheevos' code showed) and during the user's try (§1: Hardcore off by default until RetroAchievements approves the app — its server refuses hardcore unlocks from an app it doesn't know). Plan: `docs/superpowers/plans/2026-10-06-milestone-14-retroachievements.md` (13 tasks). Finish line met: all tests, Claude's emulator check with the pretend server, the impeccable finish review `ship`, and a real unlock on the user's account ("Hey, The Game Works!", game D, confirmed on retroachievements.org).
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§10 milestones)
**Previous milestone:** 13 the game check (`docs/superpowers/specs/2026-10-05-milestone-13-game-check-design.md`, tag `milestone-13`).

---

## 1. Goal and scope

Let the user earn RetroAchievements (retroachievements.org) while playing, with their own RetroAchievements account.

**User decisions (2026-10-05 and 2026-10-06):**
- RetroAchievements is the **one exception** to the "no cloud or third-party sign-ins, no app keys" rule (2026-10-05). The user's own sign-in; the app keeps the **login token, never the password**. rcheevos needs no app key.
- **Scope: the core set** — sign in, unlock pop-ups, a per-game achievements list (locked / unlocked, with badge pictures), progress indicators ("3/10"), the challenge badge, the "mastered" pop-up. **No leaderboards**, no rich presence screen, no achievements in the PC viewer.
- **Hardcore: a Settings switch, off by default until RetroAchievements approves the app** (changed by the user 2026-10-06 during their try; it was "on by default"). While on: loading states, rewind and cheats are blocked (greyed out with a one-line reason); saving states still works; fast-forward stays allowed. **Found in the user's try:** until RetroAchievements approves the app, its server says "Hardcore unlocks cannot be earned using this emulator" (it adds a warning achievement, id 101000001, which the app shows as a notice) — so hardcore starts off, and unlocks count as softcore. Turn the default back on after approval.
- **The hardcore limits apply only while signed in and the running game has a RetroAchievements set** (found while planning, 2026-10-06: otherwise every game — the user's ROM hacks included, and everyone who never signs in — would lose states, rewind and cheats). Games without a set and signed-out play keep everything.
- **Continue in hardcore** starts the game fresh from its in-game save (the hidden Continue state is not loaded) — for games with a set. Home and back without the app being closed still resumes exactly (nothing is reloaded).
- **Built as approach 1:** rcheevos in the emulator's C code; the Kotlin app does the internet requests for it and shows the pop-ups.
- **Finish line:** tests + Claude's emulator check with a fake server + **the user's try**: the user signs in on the phone emulator and unlocks a real achievement (Claude may not type the user's password).

**Out of scope:** leaderboards; rich presence display; the PC viewer and test runner as RetroAchievements clients (the glue builds on the PC for tests only); asking RetroAchievements to approve the app (done by the user when the app goes public, §8); any other online service.

## 2. What the user sees

- **Settings → RetroAchievements** (a new group):
  - signed out: Username and Password fields and a Sign in button; RetroAchievements' own message on a failure (e.g. a wrong password); nothing is saved until a sign-in succeeds;
  - signed in: "Signed in as <name>", a Sign out button;
  - the **Hardcore** switch (off by default until RetroAchievements approves the app), with one line saying what it blocks.
- **Opening a game** while signed in: the game is identified **before it starts** (so the hardcore limits and Continue are decided before the first frame): a "Checking RetroAchievements" spinner appears if this takes more than 0.3 s; after 10 s without an answer the game starts without achievements. Then a short note "N of M achievements" when the game has a set (N = unlocked by the user). Nothing when the game has no set (most ROM hacks) or when signed out. "RetroAchievements: offline" when the server can't be reached.
- **While playing** (drawn over the game, never pausing it):
  - an unlock pop-up: badge picture, title, points — a few seconds;
  - a progress indicator ("3/10") when an achievement's progress changes, a few seconds;
  - a small challenge badge while an achievement's challenge is active (shown until it ends);
  - a "Mastered" pop-up when the last achievement unlocks;
  - a short "Couldn't send an unlock" pop-up on a server error.
- **Pause menu → Achievements:** the game's list (unlocked first, then locked), badge, title, description, points; a summary line ("N of M, P points"). Shown only when the game has a set.
- **Hardcore on (and the game has a set):** in the pause menu, Load state and Cheats are greyed out with "Off in hardcore mode"; the rewind touch button and key and a Delta skin's quick load do nothing; Continue on the game list starts the game fresh. Switching hardcore **on** while such a game runs asks "Restart the game?" (no → the switch stays off); switching it **off** takes effect at once.
- **Pausing too often in hardcore:** when rcheevos refuses a pause, the game is not paused and a note says "Wait a few seconds to pause again in hardcore mode".
- All new screens and pop-ups go through the **impeccable** skill and keep to `DESIGN.md` (light / dark, the skin's accent, the phone's fonts).

## 3. rcheevos

- **rcheevos v12.5.0** (2026-09-14, MIT), the official RetroAchievements C library. Its `LICENSE`, `include/` and `src/` are committed in `third_party/rcheevos/` (downloaded with the user's OK 2026-10-06; `tools/fetch_rcheevos.py` fetches the same release again and checks its SHA-256 `e6df83de…983f`). Only the files `rc_client` needs are compiled (`third_party/rcheevos.cmake`): the client, the web API, the runtime and `md5.c` — not the file hashing, RAIntegration or external clients (it compiles cleanly with the project's clang, checked 2026-10-06).
- Built into the Android native library (`platform/android/CMakeLists.txt`) and the PC test build; not into the core.
- **The GBA memory rcheevos reads** (its own map, `consoleinfo.c`): `0x000000–0x007FFF` = IWRAM (`0x03000000`, 32 KiB), `0x008000–0x047FFF` = EWRAM (`0x02000000`, 256 KiB), `0x048000–0x057FFF` = save RAM (`0x0E000000`, up to 64 KiB). The glue maps these to the core's memory (§4); if the vendored version's map differs, the vendored map wins and this spec is updated.
- **Game identity:** rcheevos' GBA hash is the MD5 of the whole ROM file (`rc_hash_buffer`, checked in `hash.c`); the glue computes it with rcheevos' `md5.c` and loads the game by that hash (`rc_client_begin_load_game`).
- **User-Agent:** `GBAEmulator/1.0 (Android <release>) rcheevos/12.5.0` — the app's existing `Net.USER_AGENT` product name with rcheevos' own clause (`rc_client_get_user_agent_clause`). RetroAchievements recognises approved clients by this name and version.

## 4. How it is built

### 4.1 The core (`core/`)
One addition to `gba.h`, no behaviour change: a way to read the game's work RAM (IWRAM, EWRAM) as plain byte arrays, like `gba_saveram` for the save chip. No cycles, no side effects. Fingerprints, save states and speed are unchanged.

### 4.2 The glue (`platform/ra/`, C, builds on the PC and Android)
- Owns one `rc_client` for the app's whole life.
- **read_memory:** RetroAchievements addresses → the core's IWRAM / EWRAM / save RAM; 0 bytes outside them or with no game.
- **server_call:** each request (URL, POST data, content type) waits in a queue with an id; whoever does the internet takes it (`ra_take_request`, which can wait) and answers with the HTTP status and body (`ra_reply`). On Android a Kotlin thread takes them (§4.4); in PC tests a pretend server (`platform/ra/ra_fake.c`) answers them. No reply possible (no internet) = rcheevos' "retryable client error": a sign-in or game load fails, an unlock is retried later.
- Sign in with password (first time) and with the saved token; sign out; load a game by its ROM bytes; unload.
- `ra_do_frame()` after every emulated frame (fast-forward frames too); `ra_idle()` while paused.
- **Events → a small queue** of plain records (kind, achievement id, title, description, points, badge name, progress text) that the app takes out; a full queue drops the oldest.
- Hardcore on / off, `ra_can_pause()`, and the reset of achievement progress (`rc_client_deserialize_progress(NULL)`).
- rcheevos' own retry of failed unlocks (offline) is kept as is.
- **Threads (checked in rcheevos' code 2026-10-06):** rc_client locks itself, so requests and replies may come from any thread. Memory is read only inside `ra_do_frame` / `ra_idle`, which are given the game's memory each call (`rc_client_set_allow_background_memory_reads(0)`); so a game load finishes ("activates") only in one of them — before the game starts, the app's wait loop calls `ra_idle` while nothing else uses the core.
- **The hardcore limits are on** (`ra_hardcore_active`) only when the Hardcore setting is on, a user is signed in and the loaded game has achievements.

### 4.3 The game loop (`platform/android/emu_thread.c`)
- After each `gba_run_frame`: `ra_do_frame`. While paused: `ra_idle` about once a second, holding a new core lock that the other users of a paused core (state save / load, cheats, the frame copy, peeks, the app's `ra_idle`) also take.
- **Hardcore limits on:** `emu_state_load` is refused, rewinding is ignored, cheats are not set (an empty list is applied). Saving states still works.
- **Softcore:** after a state load and after a rewind ends, achievement progress is reset (rcheevos' advice: it stops wrong unlocks from half-done conditions).
- Turning hardcore on (after the user's yes) restarts the game (`gba_reset`) and reloads the game in rcheevos.

### 4.4 The app (Kotlin)
- `Net` gets POST (same timeouts, the RetroAchievements User-Agent). A request from the glue runs on a background thread; its reply goes back through the JNI bridge.
- The app polls the event queue while a game is on screen and shows the pop-ups; the Achievements list is read from the glue when opened.
- **Badge pictures:** downloaded once into `files/ra/badges/` (rcheevos gives the URL) and reused; a missing picture shows a plain placeholder.
- **Sign-in data:** `files/ra.json` (username + token). **`files/ra.json` and `files/ra/` are left out of backups** (a login, and pictures that download again) and are untouched by a restore. The **Hardcore** switch is an ordinary setting (in backups).
- No sign-in data in logs, URLs shown to the user, or crash text.
- **A pretend server in the app** (`FakeRaServer.kt`, the same answers as the C one plus a demo set that unlocks by itself over 40 s in any game: an unlock, a progress bar, a challenge, a refused unlock, and "mastered" at the end) for the on-device tests and Claude's emulator check. The debug app uses it instead of the internet only while the file `files/ra-fake` exists (made with `adb shell run-as … touch`); release builds never use it.

## 5. Problems and what happens

| Problem | What happens |
|---|---|
| Wrong username or password | RetroAchievements' message on the sign-in page; nothing saved |
| Saved token expired or revoked | signed out quietly; Settings says "Sign in again" |
| No internet when a game opens | the game plays with no achievements; "RetroAchievements: offline" note |
| Internet lost after the game loaded | unlocks wait; rcheevos sends them when it can |
| Game has no set (most ROM hacks) | nothing shown |
| The server refuses an unlock | "Couldn't send an unlock" pop-up |
| Hardcore switched on mid-game | "Restart the game?"; no → stays off |
| Too many pauses in hardcore | not paused; "Wait a few seconds to pause again" |

## 6. Testing

- **PC unit tests of the glue with a fake server:** canned RetroAchievements replies (login, game id, the set with **made-up achievements** for a test ROM, session start, award); achievements unlocked by changing the game's memory. Covered: sign in (password, token, wrong password, expired token), load (set, no set, offline), unlock event, progress, challenge, mastered, offline then retry, server error, hardcore blocks, the progress reset after a state load and a rewind, the memory map (each region, out-of-range reads).
- **Kotlin unit tests:** the request / reply path, POST, the sign-in file, `ra.json` and `files/ra/` left out of backups and untouched by restore, the hardcore greying-out rules, Continue starting fresh in hardcore.
- **On-device tests** (phone emulator) with the fake server in place of the internet.
- **Nothing may break:** CTest, every fingerprint, the locked-in games (`dev.sh games --check`), old save states keep loading, PC speed with a large made-up set switched on (`dev.sh bench --ra 300`: 300 achievements of 2 conditions each, checked every frame) ≥ **97 %** of the M13 baseline (floors in `docs/PROGRESS.md`).

## 7. Order of work (the plan's tasks)

1. rcheevos into `third_party/` and both builds. 2. The core's RAM views. 3. The glue: requests, sign-in, the pretend server. 4. The glue: games, memory, events, hardcore, the list. 5. The speed check (`gbabench --ra`). 6. The game loop and the JNI bridge. 7. Kotlin: the internet for rcheevos, the sign-in file. 8. Kotlin: the achievements controller and the pretend server; on-device tests. 9. Settings → RetroAchievements (impeccable). 10. Opening a game: identify first, Continue, the hardcore limits in the app. 11. Pop-ups, badge pictures and the Achievements list (impeccable). 12. Claude's emulator check, the finish review. 13. The user's try; milestone exit.

## 8. Finish line

Milestone 14 is done when:
- all tests pass (unit, CTest, Kotlin, on-device) and nothing in §6's "nothing may break" moved;
- Claude's check on the phone emulator with the fake server passes: every screen and pop-up, light and dark, upright and sideways, font 1.3×, and the impeccable finish review says `ship`;
- **the user's try:** the user signs in on the phone emulator and unlocks a real achievement in a game with a set (e.g. game C or game D).

**After the milestone (the user, when the app goes public):** ask RetroAchievements to approve the client by its User-Agent so hardcore unlocks count as hardcore; then make the Hardcore switch on by default again (`Settings.hardcore`).
