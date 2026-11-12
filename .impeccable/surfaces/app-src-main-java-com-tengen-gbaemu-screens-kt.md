---
version: 1
slug: "app-src-main-java-com-tengen-gbaemu-screens-kt"
primary_target: "app/src/main/java/com/tengen/gbaemu/Screens.kt"
related_targets: ["app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt","app/src/main/java/com/tengen/gbaemu/AppTheme.kt"]
---

# Game Pak — the whole app (Android, Compose)

Scope: every screen of the app — first start, game list, game sheet, pause menu, save/load slots, cheats, overlays over the game, Settings and its pages, dialogs, layout editor, app icon and launch screen. Mode: Operate. Spec: `docs/superpowers/specs/2026-10-05-milestone-12-game-pak-ui-design.md`.

Audience and job: a GBA player (the owner now, the public later) who opens the app to get back into a game in seconds, pauses, saves states, tweaks settings rarely.

Constraints: Material 3 components and system Back; phone's own fonts (Roboto, Roboto Black Italic, Roboto Condensed); light and dark follow the phone; the accent follows the chosen console skin; no Nintendo marks; the emulator core and the touch buttons over the game are untouched.

Memorable moment: the seated cartridge in the slot, and the insert when you press Continue.

The public app name: DreamBoy (decided 2026-11-12, milestone 16). Settings gained About DreamBoy with the licences and the privacy policy (see DESIGN.md, About and Document Pages).

## Direction contract

THESIS: The app is the player's Game Pak collection: every game is a moulded cartridge whose label is its own picture, and the game you were playing sits seated in the slot. It refuses the category default of a charcoal launcher, a cover grid and one neon accent.

OWN-WORLD: Moulded cartridge plastic is the ground, light grey ABS by day and smoked black by night; cards are raised carts with a top lip, a 1 dp moulded highlight and shade, and grip ridges. Labels are game art under a black label band naming the game in white Roboto Black Italic. The slot is a dark recessed bay. One see-through-plastic accent from the chosen skin (default indigo) marks only what can be pressed; label red marks danger.

STORY: The player sees their games as a physical collection, knows the seated cart is where they left off, presses Continue and is playing. Paused, the cart's label band heads a sparse menu over the frozen picture; saves read as numbered stickers.

FIRST VIEWPORT: Top: "Your games" in heavy italic, folder name under it, gear and ⋮ at right. Then a full-width slot bay holding the seated cart: 3:2 label of where the game was left, band with the name, grip strip with the accent pill Continue (primary action) and last played right-aligned. Below: a two-column grid of carts (box-art label, band name, grip strip with time).

FORM: Game Pak Labels, position 1 of my ordered list of 7, chosen by the user as IMPECCABLE'S PICK; seed key 3bf0b5f2. Signature move: the insert — the pressed cart slides up into the slot's mouth (its lip end first, as a real cartridge goes in) before the game opens; slot numbers set monumental on save stickers.

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance
