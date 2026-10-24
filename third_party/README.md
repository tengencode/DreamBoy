# Third-party code

| Folder | What | Version | Licence | From |
|---|---|---|---|---|
| `rcheevos/` | RetroAchievements' client library (`rc_client`), used by the RetroAchievements glue `platform/ra/` (milestone 14) | 12.5.0 (2026-09-14) | MIT (`rcheevos/LICENSE`) | https://github.com/RetroAchievements/rcheevos, tag `v12.5.0`; `python tools/fetch_rcheevos.py --check` compares the committed files with the release (SHA-256 pinned in the script) |

Only `LICENSE`, `include/` and `src/` are kept; `third_party/rcheevos.cmake` compiles the parts the app uses. The files are never edited here.
