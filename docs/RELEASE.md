# Releasing DreamBoy

The steps for putting out a DreamBoy release on GitHub (`https://github.com/tengencode/DreamBoy`), in order. Steps
marked **(you)** are done by the project's owner: they involve the signing key, the GitHub account or public posts.
Commands are for Git Bash in the repo folder unless said otherwise (in PowerShell, run `tools/dev.sh` as
`& "C:\Program Files\Git\bin\bash.exe" tools/dev.sh …`).

## 1. The signing key — once, ever (you)

Every release must be signed with the same key, or phones refuse the update. Make it once and keep it safe. **If the key
or its passwords are lost, DreamBoy can never be updated under the same app id again.**

In PowerShell, in the repo folder (Android Studio's Java has `keytool`):

```powershell
& "C:\Program Files\Android\Android Studio\jbr\bin\keytool.exe" -genkeypair -v -keystore dreamboy-release.jks -keyalg RSA -keysize 4096 -validity 10000 -alias dreamboy
```

Then make `keystore.properties` in the repo folder (both files are gitignored and never committed):

```
storeFile=dreamboy-release.jks
storePassword=<the store password you chose>
keyAlias=dreamboy
keyPassword=<the key password you chose>
```

The key can also live outside the repo folder (safer: it can never be committed); then `storeFile` points at it, e.g. `storeFile=../dreamboy-release.jks` for the folder above. Notepad may save the file as `keystore.properties.txt` — the name must be exactly `keystore.properties`.

Keep a copy of both files and the passwords somewhere safe outside this PC (a password manager, an offline drive).

## 2. Moving your own data across (you, once)

DreamBoy (`com.tengen.dreamboy`) is a new app next to the old development app (`com.tengen.gbaemu`), so it starts
empty. In the old app: Settings → Backup → Back up now. In DreamBoy: Settings → Backup → Restore, pick that file, then
sign in to RetroAchievements again (the sign-in is never in backups). Uninstall the old app when you no longer need it.

## 3. Each release

1. Raise the version in `app/build.gradle.kts`: `versionCode` by 1, `versionName` to the new number (`1.0.1`, `1.1.0`, …).
2. All tests pass: `bash tools/dev.sh unit`, `test`, `jvmtest`, `androidtest`, `games --check`.
3. Nothing personal: `python tools/public_scan.py --history` says `clean`. (Add anything personal it should look for to
   the gitignored `tests/public_scan.local.txt`, one per line.)
4. Build: `bash tools/dev.sh apk-release` → `app/build/outputs/apk/release/app-release.apk`.
5. Check it (`build-tools` is in `%LOCALAPPDATA%\Android\Sdk\build-tools\<version>`):
   - `aapt2 dump badging app-release.apk` shows `package: name='com.tengen.dreamboy'`, the new version and
     `application-label:'DreamBoy'`, and **no** `application-debuggable`;
   - `apksigner verify --print-certs app-release.apk` shows your key.
6. Copy it to `DreamBoy-<version>.apk` and try it on a real phone first: copy the file over and open it from the files
   app, as users will.
7. Tag and publish (the remote is `https://github.com/tengencode/DreamBoy`):
   ```bash
   git tag v<version>
   git push origin main v<version>
   gh release create v<version> DreamBoy-<version>.apk --title "DreamBoy <version>" --notes "<what changed>"
   ```
   Obtainium finds new releases by their tag, with the APK attached — always attach it.

## 4. The repo page — once (you, with Claude's commands)

```bash
gh repo edit tengencode/DreamBoy --description "DreamBoy: a fast, free Game Boy, Game Boy Color and Game Boy Advance emulator for Android" --homepage "https://github.com/tengencode/DreamBoy/releases/latest"
gh repo edit tengencode/DreamBoy --add-topic android --add-topic emulator --add-topic gba --add-topic game-boy-advance --add-topic gameboy-advance-emulator --add-topic gba-emulator --add-topic retroachievements --add-topic kotlin --add-topic arm64 --add-topic jetpack-compose --add-topic gameboy --add-topic gameboy-color --add-topic gb-emulator --add-topic gbc-emulator
```

Then on GitHub: the repo → Settings → General → Social preview → upload `docs/images/social-preview.png` (GitHub has no
command for this).

## 5. Spreading the word (you post; drafts below)

Read each place's rules on self-promotion before posting, and post from your own account. Every draft says plainly that
the app plays GBA games only for now and was built with help from an AI assistant — say it up front, it goes down
better than being found out.

### r/EmulationOnAndroid

> **Title:** DreamBoy: a new free, open-source GBA emulator for Android (GPL)
>
> I've released DreamBoy, a Game Boy Advance emulator for Android. It's free, open source (GPL-3.0) and has no ads or
> tracking.
>
> - Its own emulator core in C with hand-written ARM64 assembly, so it runs well on cheap and older phones
> - Save states, rewind, fast-forward, cheats (Action Replay, GameShark, CodeBreaker, libretro .cht files)
> - Touch layout editor, console skins, Delta skins, controllers and keyboards
> - Tilt, gyro, solar sensor and rumble cartridges, real-time clock
> - RetroAchievements (sign in with your own account); leaderboard scores and Hardcore once RetroAchievements approves the app
> - Box art, backup and restore of everything as one zip
>
> GBA only for now; Game Boy and Game Boy Color are planned. No games or BIOS included.
>
> Full disclosure: I built it with a lot of help from an AI assistant (Claude). It's checked by nearly a thousand
> automated tests, test ROMs and a 30-game check, but I'd love bug reports.
>
> Download (APK, also works with Obtainium): https://github.com/tengencode/DreamBoy/releases/latest

### Hacker News ("Show HN")

> **Title:** Show HN: DreamBoy – a GBA emulator for Android with a hand-written ARM64 core
>
> **URL:** https://github.com/tengencode/DreamBoy
>
> **Text:** DreamBoy is a free, open-source (GPL-3.0) Game Boy Advance emulator for Android. The core is C with
> hand-written ARM64 assembly and NEON for the CPU loop, drawing and sound — each fast path has a plain-C twin it is
> tested against — so it runs at full speed on old phones' small cores. The app is Kotlin and Jetpack Compose. It was
> built with heavy help from an AI assistant (Claude); every commit says so. Happy to answer questions about the
> emulator, the testing, or working that way.

### Emulation General wiki

On the wiki's Game Boy Advance emulators page, in the Android table: DreamBoy, latest version, active, GPL-3.0, link to
`https://github.com/tengencode/DreamBoy`, and a short note ("own core with ARM64 assembly; RetroAchievements; GBA only").
Follow the page's existing format.

### Awesome lists

A pull request to a list that has an Android-emulator or GBA-emulator section, one line in the list's own format:
`[DreamBoy](https://github.com/tengencode/DreamBoy) - Free, open-source GBA emulator for Android with RetroAchievements.`

### Release notes and a short post for 1.1.0 (Game Boy and Game Boy Color)

> **DreamBoy 1.1.0: Game Boy and Game Boy Color**
>
> DreamBoy now plays Game Boy (`.gb`) and Game Boy Color (`.gbc`) games next to your GBA games, with its own new
> emulator core for them: save states, rewind, fast-forward, in-game saves (the usual `.sav`, clock cartridges
> included), touch layouts of their own, console skins and box art.
>
> Not there yet for these two consoles (GBA games have them): cheats, RetroAchievements, imported Delta skins, and
> rumble and tilt cartridges. A few rare cartridge types are not supported yet; the app says so when it meets one.
>
> Nothing changes for your GBA games, saves and states. Update by installing the new APK over the old one (or let
> Obtainium do it): https://github.com/tengencode/DreamBoy/releases/latest

## 6. Later

- **Google developer verification** — before the worldwide rollout in 2027 (from then on, apps from unverified
  developers only install through Android's "advanced flow" or ADB): developer.android.com/developer-verification, the
  full-distribution account ($25 once, an ID check), then register the package name `com.tengen.dreamboy` and the
  signing key's SHA-256 (`apksigner verify --print-certs` shows it).
- **RetroAchievements approval** — after DreamBoy has been public for 6 months: ask RetroAchievements to approve it
  (its hardcore compliance requirements: docs.retroachievements.org). Once approved, switch the Hardcore default on
  (`Settings.hardcore = true`, and `SettingsTest`) and check that a real leaderboard score is sent.
