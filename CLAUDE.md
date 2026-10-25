# GBA Emulator for Android — Working Rules

## Always
- **Use layman mode** (`/layman`) for every reply: plain words, short answers. Code, commands and exact values stay exact.
- **Update all documents every time you complete a task** — before moving on or telling the user it's done, and in the task's own commit. At minimum:
  - `docs/PROGRESS.md` (the handoff file — see below): especially the task's **Completed** entry, the **Project status** tables and **Next** (the exact next task, step and first command)
  - tick the task's checkboxes in the active plan
  - the spec, if the task changed a design decision; the parent spec and the "Key files" lines below when a milestone changes
- **Always confirm that no document is stale** after every task: re-read the parts that describe the current state — PROGRESS.md's "Last updated" line, Goal, Project status, Next and Notes; the active plan's conventions and task list; the spec's status line; this file's "Key files" — and fix anything that no longer matches what is actually done, decided or true (numbers, commit hashes, branch names, paths, "next" pointers, stop points). Say in your report that you checked.
- Write `docs/PROGRESS.md` so a fresh session with **no memory of this conversation** can pick up exactly where we stopped.

## What `docs/PROGRESS.md` must contain
1. **Goal** — the overall project goal and the current milestone's goal.
2. **Project status** — which milestones (and which parts of the bigger project) are done, in progress, not started.
3. **Completed** — tasks finished, with commit hashes and short notes (surprises, workarounds, anything that deviated from the plan).
4. **Next** — the exact next task and step, with the command to run first.
5. **Notes / context** — environment facts, decisions made with the user, known problems, anything you'd need to resume after the context is cleared.

## Key files
- Design spec (whole project): `docs/superpowers/specs/2026-09-24-gba-emulator-design.md`
- Current milestone spec: **none — no milestone planned after 15** (ask the user what's next). Last finished: **milestone 15 RetroAchievements leaderboards** — `docs/superpowers/specs/2026-10-06-milestone-15-leaderboards-design.md` (tag `milestone-15`, merged into `main`). Design system: `DESIGN.md` (+ `.impeccable/design.json`); product record: `PRODUCT.md` — new UI work goes through the impeccable skill and keeps to them. (Milestone 10: 10a ✅, 10b ✅, 10c cloud sync cancelled.)
- Current plan: none. Last finished: `docs/superpowers/plans/2026-10-06-milestone-15-leaderboards.md` (9 tasks)
- Added by the user 2026-10-04: **milestone 9** button pictures + skin import — done as Delta skins ✅; **milestone 10** split into 10a `.cht` cheat files ✅ → 10b box art ✅ → 10c cloud sync ✗ cancelled. **No cloud or third-party sign-in integrations, no app keys** (user decision 2026-10-04): saves stay local; users back them up themselves — **except RetroAchievements** (user decision 2026-10-05: the user's own RA sign-in; rcheevos needs no app key). **Milestone 11** backup and restore ✅. **Milestone 12** UI overhaul "Game Pak" (added 2026-10-05, with the impeccable skill) ✅. **Milestone 13** the game check (added 2026-10-05: test many games, fix real problems; automatic only, no user check) ✅ → **milestone 14** RetroAchievements ✅ (Hardcore off by default until RetroAchievements approves the app) → **milestone 15** RetroAchievements leaderboards ✅ (added 2026-10-06; real scores are sent only in hardcore, so they wait for RetroAchievements' approval of the app).
- **Release (user decision 2026-10-23): free and open source** — no paid version, ads or in-app purchases; licence GPL-3.0-or-later (2026-10-24). Play Store vs GitHub/F-Droid and the public name not decided yet; release prep is the likely next milestone (findings in PROGRESS.md Notes, "Release prep").
- Whole-repo audit (2026-10-23, ponytail-audit): `docs/reviews/2026-10-23-ponytail-audit.md` — all 20 findings fixed (2026-10-24), and all 11 smaller ones (the last 3 on branch `small-fixes-2`, 2026-10-25, not merged yet).
- **The public repo** (user decision 2026-10-24): `~/Documents/gba_emulator_public` — work continues there. Its first commit is the cleaned state of the private repo `~/Documents/gba_emulator`, which is kept unchanged as the private archive with the full history. Commit hashes from before 2026-10-24 and the `milestone-*` tags named in the docs are in the private archive.
- Licence: GPL-3.0-or-later (`LICENSE`, user decision 2026-10-24); `README.md`; third-party code in `third_party/README.md`.
- Handoff / status: `docs/PROGRESS.md`
- When a milestone changes, update the two "current" lines above.

## How we work
- **Always execute plans inline in this session** (superpowers:executing-plans) — never subagent-driven development. Don't ask the user which execution mode to use; this is decided.
- **Go through all the plan's tasks automatically, one after another, without waiting for the user.** Stop only when the user has to check or try something on their end (e.g. the milestone's user check or a prototype trial), or when you are blocked or need a decision only they can make (e.g. a download, an emulator restart). The plan's "checkpoint" / "batch done" lines are not stops; after each task, report briefly and carry on.
- Test-first; one commit per task; commit messages end with the Co-Authored-By trailer.
- **Keep the repo public-safe** (user decision 2026-10-24): no commercial game titles and no personal details (phone serials, account names, personal paths, the user's game list or file names) in any tracked file or commit message. The user's own games are only in the gitignored `tests/games/games.local.txt` and `tests/bench/scenes.local.txt`; in docs and code they are "game A", "game B", "hack 1" and so on, and test data uses made-up names. Third-party code (`third_party/`) is left as it is.
- **Use the ponytail skill for all coding and code review** (user decision 2026-10-24): invoke `ponytail` (the smallest complete change) before writing code, `ponytail-review` to review a change, `ponytail-audit` for a whole-repo audit. Installed as skills only in `~/.claude/skills/ponytail*` (from github.com/DietrichGebert/ponytail, MIT; not the plugin's hooks). It works alongside test-first and the impeccable skill for UI, not instead of them.
- Build and test through `bash tools/dev.sh <configure|build|unit|test|run>` (Git Bash). `view` runs the viewer from an optimised `build/release` (the debug build is too slow for real games).
- Ask the user before installing software or downloading files.
