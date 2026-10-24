# Milestone 10c — Cloud Sync (Google Drive + Dropbox) — Design

**Date:** 2026-10-04
**Status:** **Cancelled by the user 2026-10-04, before any code** (local saves only — users back up their saves themselves, without the app's help; fewer integrations and app keys helps licensing and third-party audits when the app goes public). Kept as a record. Before that: brainstormed with the user 2026-10-04 (all three design parts approved: 'looks right'); spec approved by the user ('approved, write the plan'). Plan: `docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md` (13 tasks).
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§7 Kotlin app shell, §10 milestones)
**Builds on:** milestone 4c (`SaveFiles.kt`, the save file), 5a (`States.kt`, the slots and Continue), 6a (the game list: `Library.kt`, `GameId.kt`), 6b (`Settings.kt`, `SettingsScreens.kt`), 10b (`Net.kt`, the app's first internet use), all merged into `main`.

---

## 1. Goal and scope

**Milestone 10** is split (user decision 2026-10-04) into 10a cheat files ✅ → 10b box art ✅ → **10c cloud sync** (this spec), the last part.

**10c's goal:** a game's save and save states follow the user between their phones: play on one, pick up on the other exactly where they stopped. This is the app's first upload.

**Decisions (user, 2026-10-04):**
- **Devices:** phones and tablets running this app only — not the PC viewer, not other emulators.
- **What travels:** the in-game save (`.sav`) and all save states with their pictures — Continue (the automatic state), slots 1–10 and Quick. Not cheats, settings, layouts, skins, box art or the ROMs.
- **Where:** **Google Drive** (the app's hidden folder) **and Dropbox** (the app's own folder, `Apps/<app name>`); the user picks one in Settings. Google Drive was added by the user ("much more active users … comes standard with a Google account"); **both in 10c, Google Drive built first**.
- **When:** automatically — leaving a game uploads, opening a game downloads newer first, opening the app checks every game. No buttons.
- **Clash** (both phones changed the same game since they last agreed): **ask the user** — a pop-up when the game opens; the copy not picked is kept as a backup.
- **Approach 1:** per game, file by file, with a **receipt** written last (§3). Not 2 (one zipped bundle per game, re-uploaded whole every time) or 3 (a plain file mirror, which can mix a save from one phone with states from another).
- **Games are matched by fingerprint** (CRC32, `GameId`): both phones need the exact same ROM file (a state only works with the exact same ROM anyway).
- **Done when:** all tests pass, Claude's check passes with both services, and **the user's check** passes (§9).

**Not in 10c:** the PC viewer; other emulators' files; syncing cheats, settings, the game list or ROMs; more services (OneDrive, WebDAV, …); syncing while the app is closed (no background jobs); a per-game sync switch; browsing or deleting cloud files from the app; any change to the core.

## 2. What the user sees

- **Settings → Cloud sync** (a new page; a row on the main Settings page with the subtitle "Off", "Google Drive · synced 14:02" or "Dropbox · synced 14:02"). Three choices: **Off** (the default), **Google Drive**, **Dropbox**.
  - **Google Drive:** Google's own account pop-up (Play services); one tap to allow.
  - **Dropbox:** the browser opens Dropbox's sign-in; after "Allow" it comes back to the app.
  - Then: "Connected to Google Drive", the last sync time, **Disconnect**. Turning it on starts a first sync of every game in the game list.
  - Signed out (access removed, key revoked): the page shows **Sign in again**.
- **Opening a game:** if the cloud has newer data, it downloads first — a spinner "Getting your latest save" with a **Play without syncing** button; it gives up by itself after 15 s and the game opens with this phone's copy. No internet → the game opens with this phone's copy and a short note "Couldn't reach Google Drive. Playing this phone's save." (or Dropbox).
- **Leaving a game** (back to the list, Home, the app closed): what changed uploads in the background; nothing is shown unless it fails in a way the user must know (§6).
- **Opening the app / showing the game list:** every game in the list is checked quietly; newer data downloads, so Continue pictures update.
- **Clash pop-up** (when opening a game that changed on both sides): title "This game was played on two phones"; two cards, each with the Continue picture (or a plain tile), the phone's name (e.g. "phone A") and the time; **Use this one** under each. The other copy goes to the backup folder (§5). Closing the pop-up without choosing opens nothing.
- **First connect with saves already on both sides:** a game only one side has is copied over; a game both sides have and that differs gets the clash pop-up the first time it opens; identical → nothing.
- Labels follow 8a's rule: no "…" and no "&".

## 3. What's in the cloud

One folder per game, named after its fingerprint (`<id>` = 8 hex digits):

```
<id>/receipt.json
<id>/<sha256 of the file's bytes>      (one per file the receipt lists)
```

The receipt (`version` 1; read and written with our `Json`):

```json
{"version": 1, "game": "e86b3224", "phone": "phone A", "phoneId": "<random id of this install>",
 "time": 1759600000000, "count": 7,
 "files": {"save.sav": "<sha256>", "auto.state": "<sha256>", "auto.png": "<sha256>", "slot1.state": "<sha256>"}}
```

- `files` maps the local name (`save.sav` for `saves/<id>.sav`; `auto`, `slot1`–`slot10`, `quick` `.state` / `.png` for `states/<id>/`) to the content's SHA-256. Files are stored under their hash, so a file never changes once uploaded.
- **The receipt is uploaded last.** Until it is, the other phone sees the old, complete set — a half-done upload can never mix old and new.
- **Tidying:** after a new receipt is in place, files referenced by neither the new receipt nor the one before it are deleted (keeping the previous set means a phone in the middle of downloading it still finds its files).
- `count` goes up by one with every upload; `phoneId` + `count` tell which upload a receipt is. A receipt with `version` > 1 or unreadable → that game is skipped (left alone, both sides).
- Google Drive: everything in `appDataFolder` (hidden from the user's Drive), the path as the file's name. Dropbox: an App-folder app; the path under the app folder.

## 4. What each phone remembers

- `files/sync/<id>.json` — **the last receipt this phone agreed with** the cloud for that game (after an upload or download it is the receipt now in the cloud).
- `files/sync/cloud.json` — this install's random `phoneId`, the last sync time, and whether sync stopped for a new sign-in (`CloudPrefs`).
- The phone's name: Android's device name (`Settings.Global.DEVICE_NAME`), else `Build.MODEL`.
- `Settings.cloud` — `cloud` in the settings store: 0 off, 1 Google Drive, 2 Dropbox (other values → off).
- Dropbox's refresh token (and the sign-in's PKCE verifier while it waits) in `files/sync/dropbox.json` (app-private). Google Drive stores nothing: Play services hands out a fresh 1-hour key each time it is asked.

## 5. How a game syncs (`GameSync`)

For one game, compare this phone's files (hashed now), the agreed receipt (§4) and the cloud's receipt:

| This phone changed? (files ≠ agreed) | Cloud changed? (cloud receipt ≠ agreed) | Do |
|---|---|---|
| no | no | nothing |
| yes | no | **upload** |
| no | yes | **download** |
| yes | yes | **clash** — unless this phone's files equal the cloud's (then just agree) |

- **No agreed receipt** (first connect, or a new game): no cloud receipt → upload (if there are files); no local files → download; same files → agree; otherwise → clash.
- **Upload:** files the cloud doesn't have yet go up, then — after reading the cloud's receipt again; if it changed meanwhile, stop and leave it for the next time (it becomes a clash) — the new receipt (`count` + 1, this phone, now), then tidying (§3). The new receipt becomes the agreed one.
- **Download:** the files this phone doesn't have are downloaded and checked against their hashes, then written to `files/sync/tmp/<id>/` (a mismatch → the download is abandoned, this phone's copy kept, tried again later). Then this phone's current files for the game (`saves/<id>.sav` and `states/<id>/`) move to **`files/sync-backup/<id>/<time>/`** (the last 3 kept per game), the downloaded set moves in, and the cloud receipt becomes the agreed one.
- **Clash, user picks the cloud:** a download as above (this phone's copy goes to the backup).
- **Clash, user picks this phone:** the cloud's set is downloaded into a backup folder first (same place, so nothing is lost), then an upload.
- Only games in this phone's game list are synced; cloud folders for other games are left alone.
- **Never while the game runs:** a game being played is never touched; its sync waits until it is left.
- After `SaveFiles.import` (4c) the files simply differ from the agreed receipt → an upload next time, like any change.

## 6. How it runs, and when things go wrong

- One **sync worker** (a background thread with a job queue) does one game at a time, so "upload on leaving" always finishes before "download on opening" for the same game.
- **Jobs:** the app's start / the game list shown → every game in the list (the cloud's receipts found with one listing; only changed ones fetched); leaving a game → that game; opening a game → that game, first in the queue, the game waiting for it (≤ 15 s, or **Play without syncing**).
- **No internet / the service down:** nothing on leaving (the next sync catches up — the receipts show what's missing); on opening, the short note from §2.
- **Signed out** (a refresh fails with "not allowed"): syncing pauses; one note "Cloud sync stopped. Sign in again in Settings."; the Cloud sync page shows **Sign in again**. Nothing on the phone is touched.
- **Cloud full:** one note "Google Drive is full. Saves stay on this phone." (or Dropbox); syncing goes on trying at the next job.
- **Disconnect:** stops syncing; everything stays on the phone and in the cloud; the agreed receipts are deleted, so connecting again (or switching service) is a first connect (§5).
- Sign-in, tidying and network errors never change a game's files on the phone; only a finished, checked download does, and only with a backup.

## 7. The pieces

| Piece | What it does |
|---|---|
| `CloudDrive` (interface) | `list(prefix)`, `download(path)`, `upload(path, bytes)`, `delete(path)`; results: got / missing / failed / signed out / full |
| `GoogleDrive` | `CloudDrive` over Drive's REST API (v3, `appDataFolder`, scope `drive.appdata`), plain HTTPS like `Net`; the token from `GoogleAuth` |
| `DropboxDrive` | `CloudDrive` over Dropbox's HTTP API (`/2/files/list_folder`, `upload` with overwrite, `download`, `delete_v2`); the token from `DropboxAuth` |
| `FakeDrive` (tests) | `CloudDrive` in memory, with switches for failures, signed out, full and "stop after N uploads" |
| `GoogleAuth` | Play services' `AuthorizationClient` (`play-services-auth`): asks for `drive.appdata`, returns a 1-hour token; the pop-up only the first time |
| `DropboxAuth` | OAuth code flow with PKCE (no secret): builds the sign-in link, takes the code from the return link (`db-<app key>://…` caught by the app), trades it for a refresh token, refreshes the 4-hour key when needed |
| `SyncReceipt` | the receipt (§3): read, write, compare |
| `SyncFiles` / `GameLock` | a game's files by receipt name, swapping them in with a backup, the agreed receipts; the game being played is locked |
| `CloudPrefs` | `files/sync/cloud.json` (§4) |
| `GameSync` | §5 for one game, given a `CloudDrive` and the game's folders; pure (no Android), fully tested with `FakeDrive` |
| `SyncWorker` | §6: the queue, the jobs, the 15 s wait, the results the screens show |
| Screens | the Cloud sync page, the spinner, the clash pop-up, the notes |
| `MainActivity` | the wiring: jobs on start / leave / open, the sign-ins, the return link |

- `Net` gains POST/requests with headers and a body (the bytes up), keeping its timeouts.
- **New library:** `com.google.android.gms:play-services-auth` (the version picked in the plan) — **downloaded only with the user's OK** (the build runs `--offline`). Dropbox needs none.
- The **Dropbox app key** is written in the app's code (with PKCE it isn't a secret). Google needs no key in the code: Play services matches the app by its package name and signing fingerprint.
- **Built in this order:** the shared parts and Google Drive first, then Dropbox. The sign-ins come early in the plan so setup problems show up first.

## 8. One-time setup the user does (Claude guides step by step)

- **Google:** in Google Cloud Console — a project; the Drive API turned on; the OAuth consent screen (External, the app's name, scope `drive.appdata` — a **non-sensitive** scope, so the app can be set to **In production** without Google's review, and the 7-day sign-in limit of "Testing" apps doesn't apply); an **Android** OAuth client with package `com.tengen.gbaemu` and the SHA-1 of the debug signing key Claude reads from this PC (the app on every phone is built and signed here). About 15 minutes.
- **Dropbox:** on dropbox.com/developers — Create app → Scoped access → **App folder**; permissions `files.metadata.read`, `files.content.read`, `files.content.write`; the redirect link `db-<app key>://auth` (Dropbox staff confirm custom app links work with PKCE; if the console refuses it, the plan's Dropbox sign-in task stops and asks the user); the app key given to Claude. About 5 minutes.
- **Signing in** — on the emulator (its image has Google APIs; a Google account added by the user) and on the phones — is done by the user; Claude never types a password.

## 9. Testing, Claude's check, the user's check

- **PC (Kotlin unit tests):** `SyncReceipt` read/write/compare; `GameSync` — every row of §5's table, first connect (each case), upload then download between two "phones" on one `FakeDrive`, an upload stopped half-way (the other phone still gets the old complete set), a bad download (hash mismatch → nothing changed), both clash choices (backups present), tidying keeps the previous set, signed out / full / failed change nothing locally, an unknown receipt version is skipped; `DropboxAuth` (the PKCE link and challenge, the return link, token replies); `GoogleDrive` and `DropboxDrive` request building and reply parsing with a fake request function (like `BoxArtHelperTest`).
- **On-device (emulator):** the Cloud sync page, the spinner with Play without syncing, the clash pop-up (screen tests); swapping a game's files with a backup on the device.
- **Claude's check:** the emulator and the user's phone A signed in to the same account (installing on the phone asked first): play on one, open on the other — the save and Continue arrive; a clash made on purpose shows the pop-up and both choices work; airplane mode → the note, and it catches up after; Disconnect keeps everything. Once with Google Drive, once with Dropbox. The user's cheats, game list and states on the emulator are backed up first and put back.
- **The user's check:** play game A on one phone, continue on the other — once with Google Drive, once with Dropbox — and one clash on purpose, picking a side.
- All PC tests (CTest 120) unchanged; Kotlin and on-device tests all pass.

## 10. Files

- New: `CloudDrive.kt`, `GoogleDrive.kt`, `DropboxDrive.kt`, `GoogleAuth.kt`, `DropboxAuth.kt`, `SyncReceipt.kt`, `GameSync.kt`, `SyncWorker.kt`; tests `SyncReceiptTest`, `GameSyncTest` (+ `FakeDrive`), `DropboxAuthTest`, `GoogleDriveTest`, `DropboxDriveTest`; on-device screen and file-swap tests.
- Changed: `Net.kt` (uploads, headers), `Settings.kt` (`cloud`), `SettingsScreens.kt` (the Cloud sync page and row), `Screens.kt` (spinner, clash pop-up), `MainActivity.kt` (wiring), `AndroidManifest.xml` (the Dropbox return link), `app/build.gradle.kts` (`play-services-auth`).
