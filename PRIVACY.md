# DreamBoy privacy policy

_Last updated: 13 November 2026 (only the app's description changed: it now plays Game Boy and Game Boy Color games too; what happens with your data is the same). Applies to DreamBoy 1.0.0 and later._

DreamBoy is a free, open-source Game Boy, Game Boy Color and Game Boy Advance emulator for Android. This page says what the app does with your
data. The short version: **your games, saves and settings stay on your phone, and the app has no server of its own.**

## What stays on your phone

- Your games, save files, save states, cheats, skins, settings and box art stay in the app's own storage on your phone,
  or in folders you pick.
- Backups are files you make yourself and keep wherever you choose. The app never uploads them.
- Android's own cloud backup is switched off for this app.
- There is **no account system, no analytics, no advertising and no tracking** in DreamBoy, and no server run by us.

## When the app uses the internet

DreamBoy connects to other services in two cases only.

### RetroAchievements (only if you sign in)

RetroAchievements is an optional service, run by RetroAchievements (retroachievements.org), that adds achievements and
leaderboards to old games. If you never sign in, the app never contacts it.

If you sign in:

- Your user name and password are sent to retroachievements.org **once**, to sign in. The app does not keep your
  password. It keeps the sign-in token RetroAchievements sends back, in the app's private storage on your phone. The
  token is never put into backups.
- While you play a game, the app sends RetroAchievements: a fingerprint of the game file (so it knows which game it
  is), the achievements you unlock, the leaderboard scores you set, your progress, what you are doing in the game
  (Rich Presence, if the game has it), and the app's name and version and your Android version.
- Achievement pictures are downloaded from media.retroachievements.org.
- If an unlock or score can't be sent because you are offline, it is kept in the app's private storage and sent when
  the connection is back. Signing out deletes it.

What RetroAchievements does with this data is covered by RetroAchievements' own privacy policy on retroachievements.org.

### Box art and cheat lists (libretro)

- **Box art** (on by default; switch it off under Settings → Download box art): the app downloads the list of game names
  from raw.githubusercontent.com and the box pictures from thumbnails.libretro.com. The requests contain the game's
  name, as listed in that public database.
- **Find cheats online** (only when you tap it): the app gets the list of cheat files from api.github.com and the
  chosen file from raw.githubusercontent.com.

Nothing about you is sent in these requests. Like any website, those servers see your phone's internet (IP) address
and the app's name and version. raw.githubusercontent.com and api.github.com are run by GitHub, Inc.;
thumbnails.libretro.com is run by the libretro project. Their own privacy policies apply.

## Where the servers are

We don't run any servers. We don't know where RetroAchievements or libretro keep their servers; their own policies say
what they state about it. GitHub, Inc. is based in the United States.

## How long data is kept

- On your phone: until you delete it in the app, clear the app's data, or uninstall the app.
- By RetroAchievements, GitHub and libretro: as their own policies say.

## Your rights

Everything DreamBoy keeps is on your own phone, under your control: you can see it, back it up, or delete it at any
time by clearing the app's data or uninstalling it. Under laws like the GDPR, you can ask RetroAchievements about the
data it holds on your account, through RetroAchievements.

## Children

DreamBoy has no accounts, ads or tracking of its own. Signing in to RetroAchievements is optional and follows
RetroAchievements' own rules.

## Changes

If this policy changes, the new version will be in the app and on the project's GitHub page, with a new date at the
top.

## Contact

Questions about this policy: open an issue at https://github.com/tengencode/DreamBoy/issues
