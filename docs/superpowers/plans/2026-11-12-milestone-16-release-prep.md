# Milestone 16 — Release prep: DreamBoy 1.0 on GitHub — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task, inline in this session (CLAUDE.md: never subagent-driven). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the app as DreamBoy 1.0.0 — new name and app id, an About page with licences and the privacy policy, offline unlocks kept across restarts, a play-first README — and publish it on GitHub when the user says so.

**Architecture:** Mostly app-side Kotlin: build settings (`app/build.gradle.kts`), a new `RaOutbox` hooked into the existing `RaWorker`, two new Settings pages, assets copied in at build time. Docs and the GitHub page are files in the repo; publishing goes through `git` and `gh`, only on the user's word.

**Tech Stack:** Kotlin + Jetpack Compose (app), C11 + rcheevos 12.5.0 (one cross-check test), Gradle 8.14.3, Python 3 (the public-safe scanner), `gh` (publishing).

**Spec:** `docs/superpowers/specs/2026-11-12-milestone-16-release-prep-design.md` — read it first.

## Global Constraints

- Public name **DreamBoy**; release app id **`com.tengen.dreamboy`**; debug builds **`com.tengen.dreamboy.debug`**, label "DreamBoy debug". The Kotlin package / `namespace` stays `com.tengen.gbaemu`.
- **versionName `1.0.0`, versionCode `1`**; each later release raises `versionCode` by 1.
- User-Agent: `DreamBoy/<versionName> (Android <release>) <rcheevos clause>` for RetroAchievements, `DreamBoy/<versionName> (Android)` for other downloads — never a hand-written version.
- GitHub page: `https://github.com/tengencode/DreamBoy` (the account name `tengencode` is allowed in tracked files; nothing else personal).
- **No new network calls, integrations or app keys.** The GitHub link opens the browser.
- **No commercial game titles, no personal details** in any tracked file or commit message (CLAUDE.md).
- The README says plainly the app was built with help from an AI assistant (Claude); it never promises Game Boy / Game Boy Color today ("planned").
- Screenshots only from free homebrew games whose licence allows it.
- Hardcore stays **off by default**.
- **Nothing is pushed, published, posted or uploaded without the user's explicit OK at that moment.**
- CLAUDE.md working rules: test-first, one commit per task (Co-Authored-By trailer), the ponytail skill before code, the impeccable skill + `DESIGN.md` for UI, docs updated and checked for staleness every task, run all tasks without stopping except at the user steps (Tasks 9–10).

## Review Focus

1. **A RetroAchievements user name with characters that are URL-encoded** (a space, `+`, `&`): the outbox must sign with the decoded name, or every resent unlock is refused. → `RaOutboxTest.aUserNameIsSignedDecoded` (Task 2).
2. **A damaged or half-written outbox file** (power cut): the app starts normally and treats it as empty. → `RaOutboxTest.aDamagedFileIsEmptyNotACrash` (Task 2).
3. **The same unlock failing many times while offline** (rcheevos retries at 1 s, 2 s, … 120 s): stored once, sent once. → `RaOutboxTest.repeatedFailuresAreKeptOnce` (Task 2).
4. **Two offline scores on the same leaderboard:** both are kept and sent (the server keeps the best). → `RaOutboxTest.twoScoresOnOneBoardAreBothKept` (Task 2).
5. **The old app's backups** (`GBA Emulator backup ….zip`) in the automatic-backup folder: never deleted by DreamBoy's clean-up, and still restorable. → `BackupScheduleTest.oldAppBackupsAreNeverCleanedUp` (Task 1); restore reads the zip's contents, not its name (unchanged).

---

### Task 1: The name, the app ids and the version

**Files:**
- Modify: `app/build.gradle.kts` (`applicationId`, `versionCode`, `versionName`, a debug `applicationIdSuffix` + label placeholder), `app/src/main/AndroidManifest.xml` (`android:label="${appLabel}"`)
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt:143` ("GBA Emulator" → "DreamBoy"), `BackupSchedule.kt:42-47`, `Net.kt:15`, `RaNet.kt:24`, `MainActivity.kt:183` (pass the version)
- Modify: `tools/dev.sh:87` (`APP_ID="com.tengen.dreamboy.debug"`), `README.md` title line only (the rewrite is Task 6), `DESIGN.md` + `.impeccable/design.json` (name), `PRODUCT.md` (name, GitHub-only release), `CLAUDE.md` title, `docs/PROGRESS.md` Notes' `run-as com.tengen.gbaemu` commands → the debug id
- Test: `app/src/test/java/com/tengen/gbaemu/BackupScheduleTest.kt`, `RaNetTest.kt`, `NetTest.kt`; any test that asserts "GBA Emulator" (`git grep -n "GBA Emulator" app/src`)

**Interfaces:**
- Produces: `RaNet.userAgent(version: String, androidRelease: String, rcheevosClause: String): String`; `Net.userAgent: String` (a `@Volatile var`, default `"DreamBoy (Android)"`, set once at app start to `"DreamBoy/$version (Android)"`; replaces the `USER_AGENT` const); `BackupSchedule.fileName(now)` → `DreamBoy backup yyyy-MM-dd HH-mm.zip`.

- [x] **Step 1: Branch.** `git checkout -b milestone-16` (from `main`).
- [x] **Step 2: Write the failing tests.**
  - `RaNetTest`: `assertEquals("DreamBoy/1.0.0 (Android 16) rcheevos/12.5", RaNet.userAgent("1.0.0", "16", "rcheevos/12.5"))`; `NetTest`'s User-Agent check expects that string too.
  - `BackupScheduleTest`: rename every expected name to `DreamBoy backup …`; new `oldAppBackupsAreNeverCleanedUp`: `toDelete` on 7 `DreamBoy backup 2026-11-0N 10-00.zip` plus 3 `GBA Emulator backup 2026-10-0N 10-00.zip` returns only the 2 oldest DreamBoy ones.
- [x] **Step 3: Run** `bash tools/dev.sh jvmtest` — expected: those tests fail (wrong User-Agent, wrong names).
- [x] **Step 4: Implement.** `applicationId = "com.tengen.dreamboy"`, `versionCode = 1`, `versionName = "1.0.0"`; `buildTypes { debug { applicationIdSuffix = ".debug"; manifestPlaceholders["appLabel"] = "DreamBoy debug" }; release { manifestPlaceholders["appLabel"] = "DreamBoy"; … } }`; `BackupSchedule.OURS` matches only `DreamBoy backup …`; `MainActivity` passes `appVersion()` to `RaNet.userAgent` and sets `Net.userAgent` before any download.
- [x] **Step 5: Run** `bash tools/dev.sh jvmtest` — all pass; `bash tools/dev.sh apk` — no warnings; `aapt2 dump badging app/build/outputs/apk/debug/app-debug.apk` shows `package: name='com.tengen.dreamboy.debug' versionCode='1' versionName='1.0.0'` and `application-label:'DreamBoy debug'`.
- [x] **Step 6: On-device tests** — `bash tools/dev.sh androidtest` (asks the user first if the emulator isn't running — CLAUDE.md memory rule); expected 90/90 under the new debug id (this also runs the 2026-10-25 redraw change on a device for the first time). The old `com.tengen.gbaemu` app on the emulator is left installed.
- [x] **Step 7: Commit** — `feat: the app is DreamBoy (com.tengen.dreamboy, 1.0.0)`.

### Task 2: Unlocks made offline are kept and sent later

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/RaOutbox.kt`, `app/src/test/java/com/tengen/gbaemu/RaOutboxTest.kt`
- Modify: `RaNet.kt` (`RaWorker` calls the outbox), `FakeRaServer.kt` (an offline switch), `MainActivity.kt` (create the outbox, resend at start, clear at sign-out — `onRaSignOut`, line ~397)
- Test (C): `tests/unit/test_ra.c` — the signature cross-check

**Interfaces:**
- Produces: `class RaOutbox(file: File, now: () -> Long = System::currentTimeMillis)` with `fun sent(r: RaRequest, status: Int)`, `fun resend(send: (RaRequest) -> Net.Reply): Int` (how many were delivered), `fun clear()`, `val size: Int`; `companion object { fun awardSignature(id: Int, user: String, hardcore: Int, offset: Long): String; fun scoreSignature(board: Int, user: String, score: Int, offset: Long): String }`. `RaWorker.outbox: RaOutbox?` (`@Volatile var`). `FakeRaServer.offlineFlag: File?` — while that file exists, `reply` answers `Net.Reply(Net.NO_REPLY, null)`.
- The file is `files/ra-outbox.json` — outside `Backup.PATHS`, so never in backups (keep it that way).

**How it works (decided here):** rcheevos builds the requests (`third_party/rcheevos/src/rapi/rc_api_runtime.c:640-672`, `:748-775`): POST `r=awardachievement&u=…&t=…&a=ID&h=0|1[&m=HASH][&o=SECONDS]&v=MD5` and `r=submitlbentry&u=…&t=…&i=ID&s=SCORE[&m=HASH][&o=SECONDS]&v=MD5`. `v` = md5 of `"$id$user$h"` + (if o > 0) `"$id$o"` for an unlock, md5 of `"$board$user$score"` + (if o > 0) `"$o"` for a score. When the worker gets `NO_REPLY` for one of these, the outbox stores `url`, `post`, `contentType` and the time it was first stored, keyed by `a`+`h` (unlocks) or `i`+`s` (scores); any other status for the same key removes it. `resend` rebuilds each post with `o = old o + (now − stored)/1000` and a fresh `v`, sends it, and removes it unless the answer is `NO_REPLY` (a refusal or "already unlocked" also removes it). Resend runs once at app start (after the worker starts) and after any RetroAchievements answer that isn't `NO_REPLY`, on the worker's pool, never on the UI thread.

- [x] **Step 1: The C cross-check (proves the rule above is rcheevos').** In `test_ra.c`, `TEST(ra_award_and_score_signatures_for_the_outbox)`: `rc_api_init_award_achievement_request` with `{username "Tester", api_token "TOKEN", achievement_id 1001, hardcore 0, seconds_since_unlock 90}` → its `post_data` contains `v=e47388663084231e1df43ac2611c6687`; with `seconds_since_unlock 0` → `v=ba409acb0b83555ccca0863531b10f6d`; `rc_api_init_submit_lboard_entry_request` with `{"Tester", "TOKEN", leaderboard_id 2001, score 1234, seconds_since_completion 60}` → `v=725eb09a0efa54931ee41fa8fa759c5e`. Run `bash tools/dev.sh unit ra_award` — expected PASS straight away (it pins rcheevos' behaviour; it isn't new code).
- [x] **Step 2: Write the failing Kotlin tests** in `RaOutboxTest` (a temp folder, a fake clock, `send` lambdas):
  - `signaturesMatchRcheevos`: `awardSignature(1001, "Tester", 0, 90) == "e47388663084231e1df43ac2611c6687"`, `awardSignature(1001, "Tester", 0, 0) == "ba409acb0b83555ccca0863531b10f6d"`, `scoreSignature(2001, "Tester", 1234, 60) == "725eb09a0efa54931ee41fa8fa759c5e"`.
  - `anUnlockThatCantBeSentIsKeptAcrossARestart`: `sent(award 1001, NO_REPLY)` → a **new** `RaOutbox` on the same file has `size == 1`; after 120 s on the clock, `resend` sends one POST with `o=120` and `v=awardSignature(1001,"Tester",0,120)`; answered 200 → returns 1, `size == 0`.
  - `anAnswerRemovesIt`: `sent(award, NO_REPLY)` then `sent(same award, 200)` → `size == 0`; a 400 refusal also removes it.
  - `stillOfflineKeepsIt`: `resend` answered `NO_REPLY` → returns 0, `size == 1`, and the stored time is unchanged (the next `o` still counts from the first failure).
  - `repeatedFailuresAreKeptOnce`, `twoScoresOnOneBoardAreBothKept` (scores 1234 and 1300 on board 2001 → `size == 2`), `aUserNameIsSignedDecoded` (`u=Bob%20Smith` → signed with `"Bob Smith"`), `aDamagedFileIsEmptyNotACrash` (file holds `{"broken` → `size == 0`, `sent` still works), `otherRequestsAreIgnored` (`login2`, `lbinfo` with `NO_REPLY` → `size == 0`), `clearForgetsEverything`.
  - `FakeRaServerTest.offlineAnswersNoReply`: with `offlineFlag` pointing at an existing file, `reply(award)` → status `NO_REPLY`; file deleted → 200.
- [x] **Step 3: Run** `bash tools/dev.sh jvmtest` — expected: compile errors (`RaOutbox` unresolved), then failures.
- [x] **Step 4: Implement** `RaOutbox` (JSON through the existing `Json.kt`, written with `File.writeSafely`, `@Synchronized` methods, MD5 via `java.security.MessageDigest`, parameters decoded with `java.net.URLDecoder` and re-encoded with `URLEncoder` for `o` / `v` only); the `RaWorker` hook; `FakeRaServer.offlineFlag`; in `MainActivity`: `RaOutbox(File(filesDir, "ra-outbox.json"))` given to `RaService.worker`, `offlineFlag = File(filesDir, "ra-fake-offline")` when the pretend server is on, a resend after the worker starts, `outbox.clear()` in `onRaSignOut`.
- [x] **Step 5: Run** `bash tools/dev.sh jvmtest` and `bash tools/dev.sh unit ra_` — all pass. `bash tools/dev.sh apk` — no warnings.
- [x] **Step 6: Commit** — `feat: RetroAchievements unlocks made offline are kept and sent later`.

### Task 3: Licences and the privacy policy in the app

**Files:**
- Create: `PRIVACY.md` (repo root), `app/src/main/assets/licences/apache-2.0.txt`, `app/src/main/java/com/tengen/gbaemu/Licences.kt`, `app/src/test/java/com/tengen/gbaemu/LicencesTest.kt`
- Modify: `app/build.gradle.kts` (a `Copy` task like `copyTestRoms` putting `LICENSE` → `licences/gpl-3.0.txt`, `third_party/rcheevos/LICENSE` → `licences/rcheevos-mit.txt`, `PRIVACY.md` → `privacy.md` into a generated `main` assets folder)

**Interfaces:**
- Produces: `data class Licence(val part: String, val licence: String, val asset: String, val groups: List<String>)` (`groups`: the Maven group prefixes it covers, e.g. `listOf("androidx.")`; empty for DreamBoy and rcheevos); `object Licences { val SHIPPED: List<Licence> }` — DreamBoy itself (GPL-3.0-or-later, `licences/gpl-3.0.txt`), rcheevos 12.5.0 (MIT, `licences/rcheevos-mit.txt`), Kotlin standard library and kotlinx coroutines (Apache-2.0), AndroidX core / activity / lifecycle and Jetpack Compose UI / foundation / Material 3 / Material icons (Apache-2.0, `licences/apache-2.0.txt`). `const val PRIVACY_ASSET = "privacy.md"`.

- [x] **Step 1: Write the failing tests** in `LicencesTest`:
  - `everyShippedLibraryIsListed`: every `implementation("group:…")` line in `../app/build.gradle.kts` (the JVM tests run in `app/`) has its group covered by a `Licences.SHIPPED` entry (a list of group prefixes on each `Licence`, e.g. `androidx.`, `org.jetbrains.kotlin`), so a new dependency without a listed licence fails the build.
  - `everyLicenceTextIsInTheApp`: each `asset` exists under `src/main/assets/` or is one the copy task makes (`gpl-3.0.txt`, `rcheevos-mit.txt`).
  - `thePrivacyPolicyHasNoPlaceholders`: `../PRIVACY.md` contains none of `TODO`, `TBD`, `[`, `example.com`, and names `retroachievements.org`, `thumbnails.libretro.com`, `raw.githubusercontent.com`, `api.github.com` and `https://github.com/tengencode/DreamBoy/issues`.
- [x] **Step 2: Run** `bash tools/dev.sh jvmtest` — expected: compile errors, then failures.
- [x] **Step 3: Write `PRIVACY.md`** with exactly the content the spec lists in §3.5 (no analytics/ads/tracking/server of our own; RetroAchievements only when signed in — what is sent, token kept on the phone, never the password, not in backups; box art and cheat lists from the three hosts, which see the IP address; how long data is kept; where the servers are — only what each service states, "not known" otherwise; the user's rights; contact = the repo's issues page). Plain words, dated, no placeholders.
- [x] **Step 4: Add the Apache-2.0 text** — the standard text taken from the Android SDK's `build-tools/36.0.0/NOTICE.txt` (from "Apache License" / "Version 2.0, January 2004" to "END OF TERMS AND CONDITIONS"); no download. Then `Licences.kt` and the copy task.
- [x] **Step 5: Run** `bash tools/dev.sh jvmtest` — passes; `bash tools/dev.sh apk` and `unzip -l app/build/outputs/apk/debug/app-debug.apk | grep -E "assets/(licences|privacy)"` lists all four files.
- [x] **Step 6: Commit** — `feat: licences and the privacy policy travel with the app`.

### Task 4: The About page (impeccable)

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt` (`SettingsPage` gains `ABOUT("About DreamBoy")`, `LICENCES("Open-source licences")`, `PRIVACY("Privacy policy")`; an "About DreamBoy" row at the end of the main page), `MainActivity.kt` (opens the GitHub page with `Intent.ACTION_VIEW`)
- Test: `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt`

**Interfaces:**
- Consumes: `Licences.SHIPPED`, `Licences.PRIVACY_ASSET` (Task 3); the version from `appVersion()`.
- Produces: `const val GITHUB_URL = "https://github.com/tengencode/DreamBoy"` (in `Licences.kt`'s file or `SettingsScreens.kt`, one place only).

- [x] **Step 1: Invoke the impeccable skill** for this extension of the Game Pak surface (read the direction contract and `DESIGN.md`; no concept round — the spec fixes the content). The page shows: DreamBoy, `Version 1.0.0`, "A Game Boy Advance emulator for Android. Game Boy and Game Boy Color support is planned.", "Free software under the GPL 3.0 or later", rows "GitHub page" (opens the browser), "Open-source licences", "Privacy policy". Licences: one row per `Licence` (part, licence), tapping shows its text (scrollable, monospace not required). Privacy: the policy's text, readable (headings as headings, no raw `#`).
- [x] **Step 2: Write the failing on-device tests** in `ScreensTest`: `aboutShowsTheNameVersionAndLinks` (Settings → About DreamBoy → "Version 1.0.0", "GitHub page", "Open-source licences", "Privacy policy" shown), `licencesListEveryPartAndOpenItsText` (rows for "rcheevos" and "Jetpack Compose"; tapping rcheevos shows "MIT License"), `privacyPolicyIsShown` (shows "RetroAchievements" and no literal `## `).
- [x] **Step 3: Run** `bash tools/dev.sh androidtest` — expected: the three fail.
- [x] **Step 4: Implement** the pages; Back goes About ← Licences / Privacy, About ← main.
- [x] **Step 5: Run** `bash tools/dev.sh androidtest` — all pass (93/93); `bash tools/dev.sh jvmtest` passes. Look at the pages on the emulator light and dark, upright and sideways, font 1.3 (screenshots in `.impeccable/review/about-*.png`, gitignored); put the emulator's font and rotation back after.
- [x] **Step 6: Commit** — `feat: an About page with the licences and the privacy policy`.

### Task 5: The public-safe scanner

**Files:**
- Create: `tools/public_scan.py`, `tools/test_public_scan.py`
- Modify: top `CMakeLists.txt` (CTest `public_scan_tools`, next to `game_check_tools`), `.gitignore` (`tests/public_scan.local.txt`)

**Interfaces:**
- Produces: `python tools/public_scan.py [--history]` — exit 0 and "clean" when no banned word is found in `git ls-files` contents (and, with `--history`, in `git log -p --all` and commit messages); exit 1 with `file:line: word` lines otherwise. Banned words: each game's name and file name from `tests/games/games.local.txt` and `tests/bench/scenes.local.txt` (the user's games), any `C:\Users\<name>` / `/c/Users/<name>` path, and each line of the gitignored `tests/public_scan.local.txt` (extra words: the user adds anything personal). Case-insensitive; `third_party/` skipped (CLAUDE.md).

- [x] **Step 1: Write the failing tests** (`unittest`, a temp git repo): `test_finds_a_local_game_name_in_a_tracked_file`, `test_finds_a_users_folder_path`, `test_skips_third_party`, `test_history_finds_a_word_in_an_old_commit`, `test_clean_repo_says_clean`.
- [x] **Step 2: Run** `python tools/test_public_scan.py` — expected: fails (no module).
- [x] **Step 3: Implement** `tools/public_scan.py` (standard library only) and the CTest entry.
- [x] **Step 4: Run** `python tools/test_public_scan.py` — passes; `bash tools/dev.sh test` — CTest 123/123; `python tools/public_scan.py --history` on this repo — "clean" (fix anything it finds first).
- [x] **Step 5: Commit** — `test: a scanner keeps the public repo free of personal details`.

### Task 6: The GitHub page: README, screenshots, social preview

**Files:**
- Modify: `README.md` (rewritten), Create: `docs/BUILDING.md` (today's build section moves here), `docs/images/screenshot-*.png` (3–4), `docs/images/social-preview.png` (1280 × 640), `docs/images/get-it-on-github.png` (the badge, committed so the README loads no third-party image)

- [x] **Step 1: Check the homebrew licences** — for each homebrew game in `tests/games/games.txt`: `gh api repos/<owner>/<repo> --jq .license.spdx_id`; keep only games whose licence allows sharing pictures (an open licence, or CC); note each chosen game, its licence and author in the README's picture credits.
- [x] **Step 2: Take the screenshots** on the phone emulator with the debug app (homebrew pushed with `bash tools/dev.sh push <file>`): the game list with cartridges, a game playing with touch controls, the pause menu or achievements, a console skin — light and dark mix, upright. No commercial games, no user files, no account names visible (sign in to the pretend server if a RetroAchievements screen is shown).
- [x] **Step 3: Make the social preview** (1280 × 640: the cartridge icon, "DreamBoy", "GBA emulator for Android", one screenshot) and the badge, with whatever is on the machine (Python + Pillow if installed; otherwise an HTML page shown in the browser pane and captured). Installing anything → ask the user first.
- [x] **Step 4: Rewrite `README.md`** in this order: `# DreamBoy`; "A fast, free Game Boy Advance emulator for Android. Game Boy and Game Boy Color support is planned."; the badge → `https://github.com/tengencode/DreamBoy/releases/latest`; the screenshot row; features (from today's README); "Install" (download `DreamBoy-<version>.apk` from Releases, allow installs from the browser or files app, Obtainium for updates); "No games or BIOS come with the app. Use games you own."; RetroAchievements (sign in with your own account; Hardcore off until RetroAchievements approves the app); "Building" → `docs/BUILDING.md`; "How it was made" — built with help from an AI assistant (Claude), with the tests that check it (CTest, on-device tests, the game check); licence; privacy → `PRIVACY.md`; picture credits; the Nintendo disclaimer.
- [x] **Step 5: Check** — `python tools/public_scan.py` clean; every README link resolves to a file in the repo or the GitHub URLs above; images under 1 MB each.
- [x] **Step 6: Commit** — `docs: a README for players, screenshots and the social preview`.

### Task 7: The launch checklist and post drafts

**Files:**
- Create: `docs/RELEASE.md`

- [x] **Step 1: Write `docs/RELEASE.md`:** (1) the user's one-time signing key (`keytool -genkeypair -v -keystore dreamboy-release.jks -keyalg RSA -keysize 4096 -validity 10000 -alias dreamboy`, then `keystore.properties`; keep both safe); (2) moving their data from the old app (backup → restore → sign in again); (3) each release: raise `versionCode`/`versionName`, `bash tools/dev.sh apk-release`, check with `aapt2 dump badging` (no `application-debuggable`, `package: name='com.tengen.dreamboy'`), rename to `DreamBoy-<version>.apk`, tag `v<version>`, `gh release create v<version> DreamBoy-<version>.apk --title "DreamBoy <version>" --notes-file …`; (4) the repo's description and topics with the exact `gh repo edit tengencode/DreamBoy --description "…" --add-topic …` commands and uploading `docs/images/social-preview.png` in Settings → Social preview; (5) drafts for r/EmulationOnAndroid, a "Show HN", the Emulation General wiki entry and an awesome-list pull request — each says it's GBA-only for now and built with Claude's help; (6) later: Google developer verification before the 2027 rollout, RetroAchievements approval after 6 months public (then `Settings.hardcore = true` + `SettingsTest`).
- [x] **Step 2: Check** — `python tools/public_scan.py` clean; the commands match `tools/dev.sh` and the Global Constraints.
- [x] **Step 3: Commit** — `docs: the launch checklist and post drafts`.

### Task 8: Claude's emulator check and the finish review

- [x] **Step 1: The whole suite** — `bash tools/dev.sh unit`, `bash tools/dev.sh test`, `bash tools/dev.sh jvmtest`, `bash tools/dev.sh androidtest`, `bash tools/dev.sh games --check` — all pass (unit 485, CTest 123, `games --check` 31/31; Kotlin and on-device counts recorded).
- [x] **Step 2: On the emulator (debug app):** the launcher and game list say DreamBoy; About, Licences and Privacy read right; **an old-app backup restores** (make a backup in the old `com.tengen.gbaemu` app on the emulator, restore it in DreamBoy debug — saves and settings appear); **offline unlock across a restart** with the pretend server (`files/ra-fake` + `files/ra-fake-offline`; play until an unlock; force-stop; remove `ra-fake-offline`; start → the outbox sends it — seen in `logcat` and by `files/ra-outbox.json` being gone).
- [x] **Step 3: Reviews** — `ponytail-review` of the branch (`git diff main...milestone-16`); the impeccable finish review of the About pages (fix until `ship`); fix what they find, test-first where there is logic.
- [x] **Step 4: Clean the emulator** — remove the pretend-server files from the debug app; font, rotation and light mode back to normal.
- [x] **Step 5: Docs** — `docs/PROGRESS.md` (Completed, status, Next = Task 9), the plan's boxes, the spec's status; check nothing is stale. Commit — `docs: milestone 16 — Claude's check`.

### Task 9: The user's steps — the key, the release APK, their phone (STOP: needs the user)

- [x] **Step 1: Ask the user** to make the signing key and `keystore.properties` (`docs/RELEASE.md` §1), and to add any personal words to `tests/public_scan.local.txt`. Wait.
- [x] **Step 2: Build and check** — `python tools/public_scan.py --history` clean; `bash tools/dev.sh apk-release`; `aapt2 dump badging` shows `com.tengen.dreamboy`, `1.0.0`, label DreamBoy, not debuggable; `apksigner verify --print-certs` shows the user's key; copy to `DreamBoy-1.0.0.apk` at the repo root (gitignored by `*.apk`; never committed).
- [x] **Step 3: The user's check** — they install `DreamBoy-1.0.0.apk` on their real phone the way users will (copy it over, open it from the files app), play a game, open About and the privacy policy, and, if they want, restore their own backup and sign in to RetroAchievements. Wait for their word; fix what they find (test-first), rebuild, repeat.

### Task 10: Publish and finish the milestone (STOP: each outward step on the user's OK)

- [x] **Step 1: The user creates** (done by Claude with `gh`, the user asked for it **private first**, 2026-11-12) the public repo `tengencode/DreamBoy` (empty, no README or licence added by GitHub).
- [x] **Step 2: On their OK:** merge `milestone-16` into `main` (`git merge --no-ff refs/heads/milestone-16`), add the remote, push `main`, tag `v1.0.0` and push the tag, `gh release create v1.0.0 DreamBoy-1.0.0.apk …`, `gh repo edit` description and topics (`docs/RELEASE.md` §3–4). The user uploads the social preview.
- [x] **Step 3: Check the live page** — the README renders, the badge opens the release, the APK downloads and its SHA-256 matches the local file.
- [ ] **Step 4: The posts** — the user sends them (drafts in `docs/RELEASE.md`); Claude sends nothing.
- [ ] **Step 5: Milestone exit** — docs marked done (the spec's status, the parent spec's status and §10 row 16, `CLAUDE.md` key files: no current milestone, last finished 16, the release line; `docs/PROGRESS.md`), checked for staleness; commit and push on the user's OK; branch `milestone-16` deleted.
