# Milestone 16 — Release prep: DreamBoy 1.0 on GitHub — Design

**Date:** 2026-11-12
**Status:** Implemented — milestone 16 complete 2026-11-12: **DreamBoy 1.0.0 is public** at https://github.com/tengencode/DreamBoy (release v1.0.0 with the signed APK). Approved 2026-11-12, plan `docs/superpowers/plans/2026-11-12-milestone-16-release-prep.md` (10 tasks). Changes during the work: the About page has no label band (the impeccable finish review); the outbox also keeps unlocks on the answers rcheevos retries (429, 502–504, 521–525) (the code review); published private first at the user's request, then — after the personal-details check found the user's phone models and another app's name in the history — the history was rewritten, the GitHub repo deleted and recreated, triple-checked and made public. Not done here (user steps): the posts; later, Google developer verification (before the 2027 rollout) and the RetroAchievements approval (from 2027-05-12, six months public).
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§10 milestones)
**Previous work:** milestone 15 (tag `milestone-15`, private archive); the whole-repo audit and its fixes (`docs/reviews/2026-10-23-ponytail-audit.md`, all done 2026-10-25).

---

## 1. Intent

Put the app out in public, free and open source (GPL-3.0-or-later, decided 2026-10-23/24), under a new name: **DreamBoy**.

**The long-term goal (the user, 2026-10-27):** one app that plays **Game Boy, Game Boy Color and Game Boy Advance** games in one place, "a one stop shop". The name was chosen with that in mind. **The first release plays GBA games only**; Game Boy and Game Boy Color come in a later milestone (the user chose "after", not before release).

**Who and where (the user):** anyone. People find it on **GitHub or through Google**, download the APK from GitHub and install it themselves (sideloading). **No Play Store.**

**Success (the user):** many GitHub **stars, installs (release downloads) and forks**.
**The failure to avoid (the user):** "the app and/or repo is hard to find" — even if the app works.

So this milestone is as much about being findable and looking trustworthy on GitHub as about code.

## 2. What we learned while brainstorming (facts the design rests on)

- **Little competition on GitHub:** searching GitHub for "gba emulator android" (2026-10-26), the top real results had under 30 stars. A well-presented project can lead that search.
- **People do download APKs from GitHub:** melonDS-android's release 2.0.1 had about 390,000 downloads from its GitHub release.
- **What popular emulator pages show** (melonDS-android, Lemuroid, mGBA): a big "Get it" badge at the top, a row of 3–4 phone screenshots, a short feature list, GitHub topics (`android`, `emulator`, `gba`, `game-boy-advance`, …), and a short unique name.
- **The name:** "DreamBoy emulator" finds nothing else on the web; on GitHub only an old 2-star Game Boy emulator repo uses the name. On its own, "dreamboy" is an everyday word (search noise), so the page and description always pair it with "GBA emulator". Rejected: aGBA (reads as "a GBA" in search; unpronounceable; puts Nintendo's "GBA" in the name), Thumbcart (liked, but the user chose DreamBoy), Pakslot (sits next to casino apps in search).
- **Where people find sideloaded apps:** Obtainium (installs and updates apps straight from GitHub releases — works with no action from us beyond normal tagged releases with the APK attached), the Emulation General wiki, r/EmulationOnAndroid, "awesome" lists, Hacker News "Show HN".
- **IzzyOnDroid is not an option:** its App Inclusion Policy says it is "strongly opposed to apps which are fully or in part created by generative AI tools", and it mostly excludes games. This app's code was written with Claude, so we don't apply. **F-Droid** has no such rule and builds apps from source itself — possible later, **not in this milestone** (the user's channel is GitHub).
- **Android developer verification** (developer.android.com/developer-verification, read 2026-11-12): from 2026-09-30 only in Brazil, Indonesia, Singapore and Thailand and only for apps from participating stores; **sideloaded apps aren't affected until the worldwide rollout in 2027**. Then an app from an unverified developer installs only through the "advanced flow" (a one-time setup with a one-day wait) or ADB. Full verification costs **$25 once**, needs an **ID check**, and registers the app's **package name and signing key** fingerprint. The free "limited distribution" account only reaches 20–50 devices (Google's pages disagree) — useless for a public app. **Not needed for launch**; the user decides later (§8).
- **RetroAchievements hardcore approval** (docs.retroachievements.org, "hardcore compliance requirements"): the emulator must have been **public for at least 6 months**; a privacy policy without placeholders; a list of shipped open-source parts and their licences; a **unique User-Agent with a numeric, rising version**; **unlocks made offline must be securely cached and sent when the connection returns**; Rich Presence and leaderboards on and not switchable off in hardcore; a resume feature must drop to softcore (ours: Continue in hardcore starts the game fresh — already fine); the hardcore limits (no state loading, rewind, slow motion, frame advance, cheats; switching to hardcore resets the game) — ours meet them. Hardcore stays **off by default** until approval (decided in milestone 14).

## 3. Goals

1. **The app is DreamBoy.** Everywhere a user can see a name: the launcher label, the game list title, the launch screen, the About page, backup file names, the network User-Agent.
2. **A new app id, set now: `com.tengen.dreamboy`** (my call, the user didn't object — 2026-10-27). Nobody has the app yet, so this is the last free chance; after release, the id and the signing key can never change without users losing their data and updates. Only the `applicationId` changes — the Kotlin package / `namespace` `com.tengen.gbaemu` stays (171 files, JNI names), users never see it. **Debug builds get their own id, `com.tengen.dreamboy.debug`** (`applicationIdSuffix`, and a label such as "DreamBoy debug" — my call after the builder check): a debug build and the release are signed with different keys, so with one shared id every debug install on the user's phone would fail once the release is there, and the only way round would delete the user's saves. From now on **the release app holds the user's real data on their phone**; the phone emulator and the on-device tests use the debug app (`tools/dev.sh` `APP_ID` and the docs' `run-as` commands move to the debug id).
3. **Version 1.0.0**, one source of truth: `versionName` in `app/build.gradle.kts`; the User-Agent reads it (`DreamBoy/1.0.0 (Android 16) rcheevos/12.5`) instead of the hand-written `GBAEmulator/1.0` in `Net.kt` / `RaNet.kt`. Each release raises `versionCode`.
4. **About page** in Settings: the name, version, a one-line "GBA emulator for Android — Game Boy and Game Boy Color coming", the licence (GPL-3.0-or-later), a link to the GitHub page, **Open-source licences** (every shipped third-party part: rcheevos MIT, Kotlin, AndroidX / Jetpack Compose, anything else in the APK — with each licence's text or a link to it), and the **Privacy policy**.
5. **Privacy policy** — `PRIVACY.md` in the repo (GitHub renders it; that page's address is the policy's public URL) and the same text in the app. Truthful and complete, no placeholders. What it must say (from the code, 2026-11-12):
   - The app has no account system, analytics, ads or tracking, and no server of its own; games, saves, states, settings and backups stay on the phone.
   - **RetroAchievements** (only if the user signs in): the user name and password go to retroachievements.org once; the app keeps the login token on the phone (never the password, never in backups); game fingerprints, unlocks, scores and progress are sent to RetroAchievements; their own privacy policy applies. Badge pictures come from media.retroachievements.org.
   - **Box art and cheat lists** (box art on by default, a Settings switch): downloaded from thumbnails.libretro.com, raw.githubusercontent.com and api.github.com — those servers see the phone's IP address, nothing else is sent. "Find cheats online" opens a web page in the browser.
   - How long data is kept (the app: until the user deletes it or uninstalls; third parties: their policies), where those servers are (say what's known; don't guess), the user's rights (GDPR: everything is on their own phone; RetroAchievements data through RetroAchievements), and a contact (GitHub issues on the repo).
6. **Unlocks made offline are kept and sent later**, across app restarts: an unlock (or leaderboard entry) that can't reach the server is stored in the app's private files (not in backups, like `ra.json`) and sent when the connection returns, including after the app was closed. This is a RetroAchievements requirement (§2) and was a known gap (rcheevos only retries while the app runs). **Scope (my call after the builder check): a connection lost during a game** — the game's achievements were loaded while online, then the phone went offline. **Starting a game with no connection at all stays as today** (no achievements that session, the existing notice): that would need the sign-in and every game's achievement data kept on the phone and rcheevos run from that copy — its own design. If RetroAchievements' reviewers ask for it when the app is submitted (6 months after release), it becomes a later milestone. How it's done is left to the builder (§7).
7. **A signed release APK** — built with `bash tools/dev.sh apk-release` (exists since the audit, finding 9) and the user's signing key (§8). Not debuggable, release-signed (checked with `aapt2 dump badging`). Installs and runs on the user's real phone (phone A) as a fresh install.
8. **The GitHub page:**
   - `README.md` rewritten for people who want to **play**, not build: the name and one line ("DreamBoy — a fast, free Game Boy Advance emulator for Android"); a **"Get it on GitHub"** badge linking to the latest release; a row of 3–4 screenshots (and, if it helps, a short GIF); a short feature list (from today's README); "Game Boy and Game Boy Color support is planned"; how to install (download the APK, allow installing from the browser / files app, and that **Obtainium** can keep it updated); "no games or BIOS included, use games you own"; the Nintendo trademark disclaimer (already there). The build instructions move lower down or to `docs/BUILDING.md` (builder's call).
   - **Screenshots only of free homebrew games** whose licence allows it (checked one by one from the milestone 13 list — `tests/games/games.txt`), never commercial games, never the user's own games or files.
   - The repo's **description and topics** (`android`, `emulator`, `gba`, `game-boy-advance`, `gameboy-advance-emulator`, `retroachievements`, `kotlin`, `arm64`, …) and a **social preview image** (the picture shown when the link is shared). The description and topics can be set with `gh`; the social preview is uploaded by the user in the repo's settings (GitHub has no API for it) — we make the image file.
   - **How it was made, said plainly** (the user agreed, 2026-11-12): a short line that the project was built with the help of an AI assistant (Claude). The commit history already shows it (`Co-Authored-By: Claude` on every commit), so hiding it would look worse when found. The launch post drafts say it too.
9. **The first release:** a tag `v1.0.0` and a GitHub release with the signed APK attached as `DreamBoy-1.0.0.apk` and short release notes. Tagged releases with the APK attached are what Obtainium needs.
10. **Launch checklist** (a document, e.g. in `docs/RELEASE.md`): the steps only the user can do, in order, with exact commands where there are any (§8), plus the posts' draft texts for the user to send themselves.

## 4. Non-goals

- Game Boy / Game Boy Color support (the next big milestone after this one).
- Google Play, F-Droid, IzzyOnDroid, other stores.
- Google developer verification (later, before the 2027 worldwide rollout; §8).
- An in-app update checker or self-updater (Obtainium covers updates; no new internet calls).
- Building releases on GitHub Actions / CI (the signing key stays on the user's PC).
- A website beyond the GitHub page; a Discord server; donations.
- Changing the app's look beyond the name (the Game Pak design stays; no new logo — the cartridge icon stays).
- Renaming the Kotlin package, the repo's local folder or the C/CMake target names.
- Turning hardcore on by default (only after RetroAchievements approves the app, §8).

## 5. Anti-goals

- **Hard to find:** a page that doesn't say "GBA emulator for Android" in its first line, no screenshots, no topics.
- **Untrustworthy:** an unsigned or debuggable APK, placeholders in the privacy policy, a README that promises Game Boy games today, screenshots of commercial games, anything personal (the user's games, serials, account names — the public-safe rule in `CLAUDE.md` still holds).
- **Breaking the user's own setup without warning:** the new app id makes a separate app on the user's phones and emulator (§6).

## 6. Constraints

- Everything in `CLAUDE.md` stays: test-first, one commit per task, the ponytail skill for code, the impeccable skill and `DESIGN.md` for UI (the About page and any changed label), layman replies, docs updated every task, public-safe repo.
- **No new integrations, no app keys, no new network calls** (user decision 2026-10-04; RetroAchievements is the one exception). The About page's GitHub link opens the browser; it fetches nothing.
- **The new app id is a new app on every phone.** The old debug app (`com.tengen.gbaemu`) with the user's saves, states and RetroAchievements sign-in stays installed beside it. The way across is the app's own **backup and restore** (milestone 11): back up in the old app, restore in DreamBoy, sign in to RetroAchievements again (the token is not in backups, by design). Restore must accept backups made by the old app (named `GBA Emulator backup …`); automatic-backup cleanup only deletes the app's own names — whether it also tidies the old names is the builder's call. The user is told this before the switch, and nothing is uninstalled without asking.
- `tools/dev.sh` (`APP_ID`) and the docs' `run-as com.tengen.gbaemu` commands move to the new id.
- Release builds stay without minify (decided in the audit) unless the APK's size makes it worth it.
- Nothing is pushed, published, posted or uploaded without the user's explicit OK at that moment (outward-facing).

## 7. Left to the builder

- How offline unlocks are stored and resent (e.g. keep the failed award / leaderboard-entry requests from the Kotlin network worker in a private file and resend them at start and on reconnect; check what rcheevos 12.5 already offers, such as an unlock's time offset, and use it), and how it's tested (the pretend server can refuse while "offline").
- The About page's layout and where it sits in Settings (through impeccable, keeping to `DESIGN.md`).
- How the licence texts are gathered (by hand from the APK's dependency list, or a Gradle task) — as long as every shipped part is listed.
- `versionCode` scheme; the release APK's file layout (one universal APK with arm64-v8a and x86_64 is fine).
- The README's exact wording, badge image (GitHub-hosted, no third-party tracking images), screenshot sizes, the social preview image.
- The order of tasks, and splitting the milestone if it grows.

## 8. The user's steps (and decisions still theirs)

1. **Make the signing key** once, on their PC, and keep it and its passwords safe (losing it = no more updates under the same app): `keytool -genkeypair -v -keystore dreamboy-release.jks -keyalg RSA -keysize 4096 -validity 10000 -alias dreamboy`, then fill the gitignored `keystore.properties` (storeFile, storePassword, keyAlias, keyPassword). Claude never sees the passwords.
2. **Move their own data across** (backup in the old app → restore in DreamBoy → sign in to RetroAchievements again), when they want to.
3. **The GitHub repo:** create it (public) under the user's GitHub account **`tengencode`** (the user, 2026-11-12), named **`DreamBoy`** (my call) — so every link is `https://github.com/tengencode/DreamBoy` (README, About page, privacy policy contact = its issues page, release links). This account name is allowed in tracked files (an exception to the public-safe rule, as the author handle "Tengen" already is). Then, on their OK, Claude adds the remote, pushes `main` and the tag, creates the release with `gh`, sets the description and topics; the user uploads the social preview image.
4. **The user's check:** install the signed `DreamBoy-1.0.0.apk` fresh on their real phone, play a game, open About and the privacy policy.
5. **Spread the word (the user posts; Claude drafts):** r/EmulationOnAndroid, a "Show HN", an entry on the Emulation General wiki, pull requests to relevant awesome lists.
6. **Later, not this milestone:** Google developer verification before the 2027 worldwide rollout ($25, ID check, register `com.tengen.dreamboy` and the signing key's SHA-256); **after 6 months public**, ask RetroAchievements to approve DreamBoy, then switch the Hardcore default on (`Settings.hardcore = true` and `SettingsTest`) and check a real leaderboard submission.

## 9. Done when

- All tests pass (unit, CTest, Kotlin, on-device, `games --check`) with the new id and name.
- Claude's emulator check: DreamBoy installs as a new app, the name shows everywhere, About / licences / privacy policy read right (impeccable finish review `ship` for the changed screens), an old-app backup restores into DreamBoy, an offline unlock with the pretend server is kept across a restart and sent when back online.
- The signed release APK builds, isn't debuggable, and installs fresh on the user's phone (the user's check, §8.4).
- The README, `PRIVACY.md`, the launch checklist and the post drafts are written; no commercial titles or personal details anywhere. (The scanner used in the public-repo cleanup wasn't kept; the plan includes a small one — a script with the banned words kept in a gitignored local list, run over every tracked file and the history before publishing.)
- Publishing itself (push, release, topics, posts) happens when the user says so; the milestone is done when the release is live on GitHub — or, if the user wants to wait, when everything is ready and only their go is missing.
