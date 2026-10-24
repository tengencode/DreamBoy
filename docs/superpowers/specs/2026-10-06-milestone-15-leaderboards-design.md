# Milestone 15 — RetroAchievements leaderboards — Design

**Date:** 2026-10-06
**Status:** Implemented — milestone 15 complete 2026-10-06 (tag `milestone-15`): design approved in brainstorming and the written spec approved 2026-10-06; refined during the work (§2: the rank card's lines and the rank stamp, a board's newer card replacing its older one, 2 s board notes, counters side by side; the finish review's gap tag, your rank sticker and the table opening on your row). Plan: `docs/superpowers/plans/2026-10-06-milestone-15-leaderboards.md` (9 tasks). Finish line met: all tests, Claude's emulator check with the pretend server, the impeccable finish review `ship`, and the user's check — game D's real leaderboards and scores on the user's account ("It works").
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§10 milestones)
**Previous milestone:** 14 RetroAchievements (`docs/superpowers/specs/2026-10-06-milestone-14-retroachievements-design.md`, tag `milestone-14`) — this milestone adds what 14 left out on purpose.

---

## 1. Goal and scope

Add RetroAchievements **leaderboards**: see a game's boards and scores, and take part in them while playing.

**The catch (found in rcheevos 12.5.0's code while brainstorming, 2026-10-06):** rcheevos runs leaderboards **only in hardcore mode** (`rc_client_activate_leaderboards`: hardcore, or an internal `allow_leaderboards_in_softcore` flag that has no public setter). RetroAchievements' server refuses hardcore from an app it hasn't approved (found in milestone 14), so **until the app is approved, no real score can be sent**. Reading the boards and their scores is a plain request that needs no hardcore, so that works today.

**User decisions (2026-10-06):**
- **The full set:** a Leaderboards page in the pause menu **and** the live parts while playing (attempt started / failed notes, the live counter, the submitted score with its rank). The live parts are tested with the pretend servers; they work for real once RetroAchievements approves the app.
- **Its own pause-menu row** "Leaderboards", under "Achievements", shown only for games that have boards (not a tab inside Achievements).
- **Scores shown on a board:** the top 10, then (if the user isn't in it) a few rows around the user's rank, the user's row marked. Rank, name and score only — **no profile pictures**.
- **A Settings switch** "Leaderboard notes on screen" (Settings → RetroAchievements), **on by default**: off hides the live counter and the started / failed notes; the submitted-score card still shows, and scores are sent either way.
- **Built as approach 1:** through rcheevos in the existing C glue (the same request queue, the same Kotlin internet worker, the same event queue). Not the app calling the website itself.
- **Finish line:** all tests + Claude's emulator check with the pretend server (impeccable `ship`) + **the user opens a real game's Leaderboards** on the phone emulator, signed in with their own account, and sees the real scores. Sending a real score waits for the approval.

**Out of scope:** sending a real score before approval; profile pictures; scrolling a whole board; friends' scores; rich presence; leaderboards in the PC viewer; softcore leaderboards (rcheevos doesn't allow them).

## 2. What the user sees

- **Pause menu → Leaderboards** (only when signed in and the game has boards; works with Hardcore on or off):
  - the list of boards: title and description;
  - tap a board → its scores: a spinner while loading, then the top 10 and, when the user ranks below 10, a gap and up to 5 rows around the user; the user's row marked; the total ("N players"); a lower-is-better board reads the same way (rank 1 at the top — the server orders it);
  - "You haven't set a score yet" when the user isn't on the board; "No scores yet" for an empty board;
  - offline: "Couldn't load scores — no connection" and a **Try again** button;
  - with Hardcore off, one line on the page: "Scores are only sent in Hardcore mode".
- **While playing, Hardcore on and the game has boards:**
  - a short note "Leaderboard started: <title>" when an attempt starts and "Leaderboard failed: <title>" when it fails (hidden by the switch);
  - a **live counter pill** per active attempt (rcheevos' display text, e.g. a time "1:23.45" or a score), on the scrim like the progress pill; at most 3 shown, side by side in one row 6 dp under the badge pills, 8 dp apart (hidden by the switch);
  - when an attempt ends: a card with the board's name and "Score <score> · sending", replaced — when the server answers — by the rank card: the board's name, "Score <score> · M players" (and "· best <best>" when this one wasn't better) and the rank stamped at the right like an unlock's points ("#N", "rank"); a board's newer card takes the place of its older one, so the rank never waits behind its own "sending" card (refined in Task 6 after the emulator check: the line first said "Rank N of M", which repeated the stamp);
  - the started / failed notes show for 2 s (the other notes 3 s);
  - a server refusal (expected before approval): a card "Couldn't send a score" with the server's message.
- **Settings → RetroAchievements:** the new switch "Leaderboard notes on screen" (on by default), with one line saying what it hides.
- All new screens and pop-ups go through the **impeccable** skill as an extension of the Game Pak surface and keep to `DESIGN.md` (light / dark, the skin's accent, the phone's fonts) — like milestone 14's screens.

## 3. rcheevos facts (12.5.0, read 2026-10-06)

- Leaderboard events from `rc_client`: `LEADERBOARD_STARTED`, `LEADERBOARD_FAILED`, `LEADERBOARD_SUBMITTED` (the attempt ended; the score goes to the server), `LEADERBOARD_SCOREBOARD` (the server's answer: submitted score, best score, new rank, number of entries, top entries), `LEADERBOARD_TRACKER_SHOW / UPDATE / HIDE` (the live counter: an id and display text up to 24 characters). The scoreboard data is valid only inside the event callback — the glue copies what it needs.
- The board list: `rc_client_create_leaderboard_list` (title, description, id, format, `lower_is_better`); `rc_client_has_leaderboards`.
- Scores: `rc_client_begin_fetch_leaderboard_entries(id, first, count)` and `rc_client_begin_fetch_leaderboard_entries_around_user(id, count)` — asynchronous, through the same request path (`r=lbinfo`); each entry has user, display text, rank, index; the list has the total and the user's index.
- Submissions are retried by rcheevos when offline, like unlocks (milestone 14 notes).
- Switching hardcore off deactivates the boards and raises tracker-hide events.

## 4. How it is built

### 4.1 The glue (`platform/ra/`)
- **New event kinds** in the existing queue: `LB_STARTED`, `LB_FAILED` (id, title), `LB_SUBMITTED` (id, title, score), `LB_SCOREBOARD` (id, title, submitted score, best score, rank, entries), `LB_TRACKER_SHOW / UPDATE / HIDE` (tracker id, display text). A waiting tracker update is **replaced** by a newer one for the same tracker (it changes every frame; the same rule as progress updates, so it can't push other events out).
- **New calls:** `ra_has_leaderboards`; `ra_leaderboards` (id, title, description, lower-is-better; at most a fixed number); `ra_fetch_entries(id)` — starts both fetches (top 10, and 11 around the user); `ra_entries_status` (`RA_BUSY` / `RA_OK` / `RA_FAILED` + code) and `ra_entries` (the merged rows: rank, name, score, is-the-user, plus a gap marker between the top 10 and the rows around the user; duplicates removed; the total; whether the user is on the board). **Only the newest fetch counts** (a generation number, as game loads); unloading the game or signing out drops it.
- `ra_fake.c` (PC pretend server): the made-up set gets 2 boards (a time board, lower is better; a score board); it answers `submitlbentry` and `lbinfo` (a board with ~30 entries, the test user at rank 17; an empty board), plus an offline and a refused mode.

### 4.2 The app (Kotlin)
- Bridge functions for the new calls (`NativeBridge.ra*`, `jni_ra.c`), text as UTF-8 bytes as in milestone 14.
- `RaPopups`: the new event kinds — the started / failed notes, the submitted and scoreboard cards, the tracker pills (keyed by id, at most 3 shown, side by side); the switch hides notes and pills, not the cards.
- `Settings`: `leaderboardNotes` (on by default; in backups like the other settings).
- **The Leaderboards panel** (`AchievementScreens.kt` or a new file if it grows large): the board list, a board's scores, polling `ra_entries_status` while open, Try again.
- `FakeRaServer.kt` (the app's pretend server): the demo set gets 2 boards — one that starts by itself, counts for a few seconds and submits (answered with a rank), one that starts and fails; it serves `lbinfo` for both. Its demo only shows with Hardcore on (rcheevos' rule).

## 5. Problems and what happens

| Problem | What happens |
|---|---|
| Offline when opening a board | "Couldn't load scores — no connection", Try again; no background retry |
| Offline when a score is sent | rcheevos keeps and retries it; the existing "RetroAchievements: offline" note; the rank card when it gets through |
| The server refuses a score (before approval) | "Couldn't send a score" card with the server's message |
| The user isn't on the board | top 10 + "You haven't set a score yet" |
| Empty board | "No scores yet" |
| The game closes while scores load | the answer is thrown away |
| Hardcore switched off during an attempt | rcheevos stops the boards; the pills disappear |
| State loads, rewind, cheats | already blocked in hardcore (milestone 14) |

## 6. Testing

- **PC unit tests of the glue with the pretend server:** the board list; started / failed / submitted / scoreboard events; tracker updates replacing each other without pushing other events out; the fetch (top 10 + around the user merged, duplicates removed, the user's row marked, the gap, the total; the user in the top 10; not on the board; an empty board); offline and refused answers; the newest fetch wins; nothing runs in softcore.
- **Kotlin unit tests:** the new event kinds in `RaPopups`; the switch hiding notes and pills but not the cards; at most 3 pills; reading the score rows; the setting's default and backup.
- **On-device test** (pretend server): the demo board starts, submits and gets its rank; a board's scores load.
- **Nothing may break:** CTest, every fingerprint, `dev.sh games --check`, old save states keep loading; `gbabench --ra 300` also gets made-up boards and stays ≥ the 97 % floors (`docs/PROGRESS.md`).

## 7. Order of work (the plan's tasks, roughly)

1. The glue: leaderboard events and tracker updates (+ the PC pretend server's boards). 2. The glue: the board list and the score fetch. 3. The speed check with boards. 4. The JNI bridge. 5. Kotlin: `RaPopups`, the setting, the app's pretend server, on-device test. 6. The pop-ups and pills on screen (impeccable). 7. The Leaderboards page in the pause menu (impeccable). 8. The Settings switch. 9. Claude's emulator check and the finish review. 10. The user's check; milestone exit.

## 8. Finish line

Milestone 15 is done when:
- all tests pass (unit, CTest, Kotlin, on-device) and nothing in §6's "nothing may break" moved;
- Claude's check on the phone emulator with the pretend server passes: every new screen, note, pill and card, light and dark, upright and sideways, font 1.3×, and the impeccable finish review says `ship`;
- **the user's check:** signed in with their own account on the phone emulator, the user opens a real game's Leaderboards and sees the real scores.

**After the milestone (the user, when the app goes public):** with RetroAchievements' approval, hardcore and so leaderboard submissions start working for real; nothing in the app needs changing for that (the Hardcore default goes back on, as milestone 14 notes).
