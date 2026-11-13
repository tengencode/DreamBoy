# Third-party code

| Folder | What | Version | Licence | From |
|---|---|---|---|---|
| `rcheevos/` | RetroAchievements' client library (`rc_client`), used by the RetroAchievements glue `platform/ra/` (milestone 14) | 12.5.0 (2026-09-14) | MIT (`rcheevos/LICENSE`) | https://github.com/RetroAchievements/rcheevos, tag `v12.5.0`; `python tools/fetch_rcheevos.py --check` compares the committed files with the release (SHA-256 pinned in the script) |

Only `LICENSE`, `include/` and `src/` are kept; `third_party/rcheevos.cmake` compiles the parts the app uses. The files are never edited here.

## Data taken from other projects

| Where it is | What | Licence | From |
|---|---|---|---|
| `core_gb/src/dmg_looks.c` (the tables only; the code around them is ours) | The Game Boy Color's colours for grey Game Boy games: the title sums it knows, the fourth letters, the combination for each sum, the combinations and the colours (milestone 17c) | Expat / MIT, Copyright (c) 2015-2026 Lior Halphon (`sameboy-boot-rom-LICENSE.txt`) | SameBoy's boot ROM source, https://github.com/LIJI32/SameBoy `BootROMs/cgb_boot.asm` (read 2026-11-13) |
