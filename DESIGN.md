---
name: DreamBoy
description: A Game Boy Advance emulator for Android whose screens are moulded cartridge plastic and whose games are a Game Pak collection.
colors:
  accent-indigo: "#4B3FB8"
  accent-indigo-bright: "#928BD7"
  on-accent-bright: "#19153D"
  plastic-grey: "#C5C7CC"
  cart-grey: "#D7D9DD"
  cart-grey-high: "#E3E4E7"
  moulded-highlight-grey: "#EEEFF1"
  moulded-shade-grey: "#A3A5AB"
  plastic-smoked: "#141518"
  cart-smoked: "#222327"
  cart-smoked-high: "#2C2D32"
  moulded-highlight-smoked: "#34353B"
  moulded-shade-smoked: "#070708"
  slot-bay-grey: "#26272B"
  slot-bay-smoked: "#0A0A0C"
  slot-mouth-grey: "#141518"
  slot-mouth-smoked: "#000000"
  label-band-grey: "#141518"
  label-band-smoked: "#000000"
  label-band-edge-smoked: "#34353A"
  label-stock: "#F2F2F4"
  ink-day: "#16171B"
  ink-soft-day: "#44464D"
  outline-day: "#8E9097"
  ink-night: "#ECECEF"
  ink-soft-night: "#A6A8AF"
  outline-night: "#4A4C52"
  label-red-day: "#9E0E27"
  label-red-soft-day: "#F0D5D9"
  label-red-night: "#FF8C98"
  on-label-red-night: "#3A0710"
  label-red-soft-night: "#3A1A1F"
  white: "#FFFFFF"
typography:
  display:
    fontFamily: "Roboto (sans-serif), Black Italic"
    fontSize: "84sp"
    fontWeight: 900
    lineHeight: "80sp"
    letterSpacing: "-1sp"
  headline:
    fontFamily: "Roboto (sans-serif), Black Italic"
    fontSize: "30sp"
    fontWeight: 900
    lineHeight: "36sp"
    letterSpacing: "-0.3sp"
  headline-small:
    fontFamily: "Roboto (sans-serif), Black Italic"
    fontSize: "26sp"
    fontWeight: 900
    lineHeight: "32sp"
    letterSpacing: "-0.2sp"
  title:
    fontFamily: "Roboto (sans-serif), Black Italic"
    fontSize: "18sp"
    fontWeight: 900
    lineHeight: "24sp"
  title-small:
    fontFamily: "Roboto (sans-serif), Black Italic"
    fontSize: "15sp"
    fontWeight: 900
    lineHeight: "19sp"
  body:
    fontFamily: "Roboto (sans-serif)"
    fontSize: "16sp"
    fontWeight: 400
    lineHeight: "24sp"
    letterSpacing: "0.5sp"
  body-small:
    fontFamily: "Roboto (sans-serif)"
    fontSize: "12sp"
    fontWeight: 400
    lineHeight: "16sp"
  label:
    fontFamily: "Roboto (sans-serif)"
    fontSize: "14sp"
    fontWeight: 700
    lineHeight: "20sp"
    letterSpacing: "0.1sp"
  tag:
    fontFamily: "Roboto Condensed (sans-serif-condensed)"
    fontSize: "13sp"
    fontWeight: 700
    lineHeight: "16sp"
    letterSpacing: "0.2sp"
rounded:
  print-corner: "3px"
  label: "4px"
  sticker: "6px"
  skin-tile: "8px"
  small: "10px"
  cartridge: "14px"
  card: "18px"
  slot-bay: "20px"
  sheet: "22px"
  pill: "9999px"
spacing:
  xs: "4px"
  sm: "8px"
  md: "12px"
  gutter: "14px"
  lg: "16px"
  xl: "20px"
  xxl: "24px"
components:
  button-accent:
    backgroundColor: "{colors.accent-indigo}"
    textColor: "{colors.white}"
    rounded: "{rounded.pill}"
    padding: "0 24px 0 18px"
    height: "48px"
  button-accent-night:
    backgroundColor: "{colors.accent-indigo-bright}"
    textColor: "{colors.on-accent-bright}"
    rounded: "{rounded.pill}"
    height: "48px"
  button-outlined:
    backgroundColor: "{colors.plastic-grey}"
    textColor: "{colors.ink-day}"
    rounded: "{rounded.pill}"
    height: "48px"
  button-danger:
    backgroundColor: "{colors.label-red-day}"
    textColor: "{colors.white}"
    rounded: "{rounded.pill}"
    height: "48px"
  cartridge:
    backgroundColor: "{colors.cart-grey}"
    textColor: "{colors.ink-day}"
    rounded: "{rounded.cartridge}"
    padding: "8px"
  label-band:
    backgroundColor: "{colors.label-band-grey}"
    textColor: "{colors.white}"
    typography: "{typography.title-small}"
    rounded: "{rounded.label}"
    height: "40px"
  label-band-seated:
    backgroundColor: "{colors.label-band-grey}"
    textColor: "{colors.white}"
    typography: "{typography.title}"
    rounded: "{rounded.label}"
    height: "48px"
  slot-bay:
    backgroundColor: "{colors.slot-bay-grey}"
    textColor: "{colors.white}"
    rounded: "{rounded.slot-bay}"
  settings-group:
    backgroundColor: "{colors.cart-grey}"
    textColor: "{colors.ink-day}"
    rounded: "{rounded.card}"
  nav-row:
    textColor: "{colors.ink-day}"
    typography: "{typography.body}"
    padding: "10px 16px"
    height: "64px"
  save-sticker-printed:
    backgroundColor: "{colors.label-stock}"
    textColor: "{colors.ink-day}"
    typography: "{typography.display}"
    rounded: "{rounded.sticker}"
    padding: "7px 7px 2px 7px"
  save-sticker-empty:
    backgroundColor: "{colors.cart-smoked-high}"
    textColor: "{colors.ink-night}"
    typography: "{typography.display}"
    rounded: "{rounded.sticker}"
  choice-chosen:
    backgroundColor: "{colors.accent-indigo}"
    textColor: "{colors.white}"
    rounded: "{rounded.pill}"
  choice-unchosen:
    backgroundColor: "{colors.plastic-grey}"
    textColor: "{colors.ink-day}"
    rounded: "{rounded.pill}"
  badge:
    backgroundColor: "{colors.label-stock}"
    rounded: "{rounded.label}"
    size: "52px"
  badge-missing:
    backgroundColor: "{colors.cart-grey-high}"
    textColor: "{colors.ink-soft-day}"
    rounded: "{rounded.label}"
    size: "52px"
  achievement-card:
    backgroundColor: "{colors.cart-grey}"
    textColor: "{colors.ink-day}"
    typography: "{typography.title-small}"
    rounded: "{rounded.cartridge}"
    padding: "0 12px"
  overlay-pill:
    textColor: "{colors.white}"
    typography: "{typography.tag}"
    rounded: "{rounded.pill}"
    padding: "4px"
  overlay-pill-counter:
    textColor: "{colors.white}"
    typography: "{typography.tag}"
    rounded: "{rounded.pill}"
    padding: "0 12px 0 10px"
    height: "36px"
  board-mark:
    backgroundColor: "{colors.cart-grey-high}"
    textColor: "{colors.ink-soft-day}"
    rounded: "{rounded.label}"
    size: "52px"
  board-mark-note:
    backgroundColor: "{colors.cart-grey-high}"
    textColor: "{colors.ink-soft-day}"
    rounded: "{rounded.label}"
    size: "40px"
  rank-stamp:
    textColor: "{colors.ink-day}"
    typography: "{typography.title}"
  achievement-row:
    textColor: "{colors.ink-day}"
    typography: "{typography.title-small}"
    padding: "10px 0"
  leaderboard-row:
    textColor: "{colors.ink-day}"
    typography: "{typography.title-small}"
    padding: "10px 0"
    height: "64px"
  score-row:
    textColor: "{colors.ink-day}"
    typography: "{typography.body}"
    padding: "8px"
    height: "44px"
  score-row-you:
    backgroundColor: "{colors.cart-grey-high}"
    textColor: "{colors.ink-day}"
    typography: "{typography.body}"
    rounded: "{rounded.label}"
    padding: "8px"
    height: "44px"
  rank-sticker:
    backgroundColor: "{colors.label-stock}"
    textColor: "{colors.ink-day}"
    typography: "{typography.title-small}"
    rounded: "{rounded.print-corner}"
    padding: "1px 6px"
  gap-tag:
    textColor: "{colors.ink-soft-day}"
    typography: "{typography.tag}"
    padding: "6px 0"
  share-bar-track:
    backgroundColor: "{colors.moulded-shade-grey}"
    rounded: "{rounded.pill}"
    height: "6px"
  share-bar-fill:
    backgroundColor: "{colors.ink-day}"
    rounded: "{rounded.pill}"
    height: "6px"
---

# Design System: DreamBoy

## Overview

**Creative North Star: "The Game Pak Collection"**

The app is the player's shelf of cartridges. Every screen is moulded cartridge plastic: light grey ABS by day and smoked black by night, switching with the phone's dark mode. Cards are not floating Material sheets but raised carts standing on that plastic, each with a narrower lip along its top edge, a 1 dp moulded highlight on top, a short shade below and fine grip ridges near the bottom. Each game's label is its own picture (box art or the frame where the player left off) under a black label band that names the game in white heavy italic. The game last played sits seated in a dark recessed slot at the top of the list, and pressing Continue slides it up into the slot's mouth before the game opens.

This is an Operate-mode app. Navigation, sheets, dialogs, switches and segmented choices are standard Material 3 components with themed colours, and the world comes through in palette, lettering, shape and one signature motion. The density is calm and physical: a few large objects per screen, generous gutters, nothing tinted for decoration. Colour comes from the game art and from one see-through-plastic accent that follows the chosen console skin. It refuses the category default of a charcoal launcher with a cover grid and a single neon accent, and it never prints Nintendo's marks.

The chrome frames the game picture and never competes with it. Over a paused game, the menu is a cartridge panel on a dark scrim, headed by that game's label band.

**Key Characteristics:**
- Two plastics, one structure: grey by day, smoked by night, the same roles in both; the slot and label bands stay dark in both.
- Games are cartridges; their labels are their pictures; names sit on black bands in white Roboto Black Italic.
- One accent, derived from the console skin's plastic, marks only what can be pressed.
- Label stock (pale print) is the same in both themes and appears only where something is actually printed.
- Depth comes from moulding (highlight and shade lines) and recessing, not drop shadows.
- The phone's own fonts only: Roboto, its Black Italic, and Roboto Condensed.

## Colors

A committed plastic palette: neutral greys own every surface, black label bands carry names, the game art supplies most of the colour, and one skin-derived accent marks what can be pressed.

### Primary
- **See-through Indigo** (accent-indigo): the default accent in the light theme, as the Continue and Resume pills, filled buttons, the chosen segment of a choice row, switches that are on, focus, and the chosen skin's ring. White text sits on it (7.8:1), and it reads 4.6:1 as text on the grey plastic.
- **Bright Indigo** (accent-indigo-bright): the default accent in the dark theme, lightened until it reads 4.5:1 on the lightest smoked card. Its text and icons use **Indigo Ink** (on-accent-bright), the same hue very dark (5.7:1). This bright form is also the accent for marks drawn over the game or a skin, whatever the theme (the layout editor's selection outline).
- **The skin decides the hue.** Each built-in console skin names a seed (Ice, Pink, Lime, Orange, Yellow, Gold, Red and cream, Grey and red, Classic). The accent keeps the seed's hue and saturation and only moves its lightness. Light theme: darker until white on it is at least 4.5:1 and it is at least 4.5:1 on the grey plastic. Dark theme: lighter until it is at least 4.5:1 on cart-smoked-high, with its on-colour darkened in the same hue until at least 4.5:1. Plain, White, Black, Silver and imported skins use the indigo default. The app icon and the Android 12+ splash icon background use the indigo default.

### Tertiary
- **Label Red** (label-red-day / label-red-night, with label-red-soft-* as its container and on-label-red-night as text on it at night): Quit, Remove, Delete, the lost-folder message and error text (an unlock or a score that couldn't be sent, scores that couldn't load). Nothing else.

### Neutral
- **Grey Plastic / Smoked Plastic** (plastic-grey / plastic-smoked): the ground of every screen, the window background at launch, and the status and navigation bars.
- **Cart Grey / Smoked Cart** (cart-grey / cart-smoked): cartridge bodies, settings groups, the pause panel, sheets and dialogs.
- **Cart High** (cart-grey-high / cart-smoked-high): the lightest ground in each theme: an empty save slot, secondary containers.
- **Moulded Highlight and Shade** (moulded-highlight-* / moulded-shade-*): the 1 dp top edge and 1.5 dp lower lip that make a cart look moulded; the shade also draws the grip ridges.
- **Slot Bay and Slot Mouth** (slot-bay-* / slot-mouth-*): the recessed bay that holds the seated cart or the empty-slot outline. Dark in both themes, like a real console's slot.
- **Label Band** (label-band-grey / label-band-smoked): black strips naming a game, in white. At night the band gets a 1 dp edge (label-band-edge-smoked) so it separates from the smoked plastic.
- **Label Stock** (label-stock): the pale printed paper behind box art, blank labels and filled save stickers. It is print, so it stays the same in both themes and always carries the day inks (ink-day, ink-soft-day: 16.0:1 and 8.4:1).
- **Ink, Soft Ink, Outline** (ink-*, ink-soft-*, outline-*): text, secondary text such as times and help notes, and dividers or chevrons. outlineVariant is the outline at 45% over the cart.

### Named Rules
**The Pressable Accent Rule.** The accent marks only what can be pressed or what is switched on: filled buttons, the chosen segment, an on switch, a chosen ring, focus. Never a heading, a divider, an icon at rest or a decorative fill.

**The Label Red Rule.** Red means danger and errors. It is never a second accent, and danger actions sit apart from the safe ones (after a divider in sheets, last in the pause menu).

**The Printed Stock Rule.** Label stock appears only where something is printed: a box, a picture, a blank label, a filled save slot, a downloaded badge, and the user's own rank in a score table (a small printed sticker in day ink). An empty save slot, a badge not yet downloaded and a leaderboard's mark are bare plastic (cart-high), so those pages stay dark at night.

**The Contrast Floor Rule.** Every text pairing and every derived accent is at least 4.5:1, enforced by the palette and accent tests; a new colour is checked against the darkest light ground (grey plastic) and the lightest dark ground (cart-smoked-high).

## Typography

**Display Font:** Roboto Black Italic (the phone's sans-serif at weight 900, italic)
**Body Font:** Roboto (the phone's sans-serif)
**Label/Mono Font:** Roboto Condensed Bold (sans-serif-condensed) for small tags

**Character:** Heavy slanted lettering like the print on a cartridge label, against plain Roboto for everything you read. No font is bundled or downloaded.

### Hierarchy
- **Display** (900 italic, 84sp, line 80sp, -1sp): the slot numbers on save stickers, with tabular figures.
- **Headline** (900 italic, 30sp): the app name on the first start.
- **Headline small** (900 italic, 26sp): screen titles in the header ("Your games", "Settings", "Save state") and, as Material default, dialog titles.
- **Title** (900 italic, 18sp): names on the seated cart's band and in sheet and pause-panel bands; the Resume label; an achievement's points and a score's rank stamp ("#4", tabular).
- **Title small** (900 italic, 15sp): names on grid carts' bands (two lines); achievement and leaderboard names on the pop-up cart and in the Achievements and Leaderboards lists; ranks in a score table (tabular); Continue uses it at 17sp.
- **Body** (Roboto 400, 16sp; 500 for row titles and menu rows): settings rows, menu rows, explanations, names and scores in a score table (the user's name bold).
- **Body small** (Roboto 400, 12sp): summaries, help notes under a setting, times on carts and stickers, the line under a board's name on its pop-up card (tabular).
- **Document heading** (Roboto 700, upright; 18sp for a section, 16sp for a part of one): headings inside a document shown in the app, such as the privacy policy; more space above than below.
- **Label** (Roboto 700, 14sp): Material button labels.
- **Tag** (Roboto Condensed 700, 13sp, 0.2sp): short tags in sentence case: "Empty" on a save slot, the label above a choice row in the pause menu, key-table column heads, the speed pill, the achievement progress count and the leaderboard counters over the game, the "pts" under points and the "rank" under a rank stamp, an achievement's progress in the list, the gap in a score table ("Ranks 11 to 14").

### Named Rules
**The Label Lettering Rule.** Roboto Black Italic is for names and numbers that would be printed on a cartridge: screen titles, game names on bands, slot numbers, achievement and leaderboard names, points and ranks, the Continue and Resume labels. Running text, settings and messages (a server problem, a score that couldn't be sent, a note) stay in upright Roboto; so do players' names and scores in a score table, and a document's headings (the privacy policy's).

**The Tabular Times Rule.** Times, dates, counts, scores, ranks and slot numbers use tabular figures (tnum) so they line up and a running counter doesn't jitter; times on carts and scores in a table sit right-aligned or in the grip strip.

## Layout

Single-column phone layout with system bars padded. Headers are 64dp minimum with a 20dp start inset (4dp with a back arrow). The game list is an adaptive grid with a 152dp minimum column (two columns upright, more sideways), 16dp side padding, 14dp horizontal and 16dp vertical gutters; the slot spans the full row above it. The seated cart is the slot's width minus 40dp; when the slot is wider than 560dp (sideways or tablet) it turns landscape, up to 680dp wide, with the label (180-300dp) on the left and the time and Continue beside it. The pause panel is at most 420dp wide upright and 720dp sideways (two columns when the screen is wide and under 520dp tall), with 16dp margin from the screen. Save stickers sit two per row with 12dp gaps. Settings groups are full width with 6dp vertical spacing; rows use 16dp horizontal and 10-14dp vertical padding. The rhythm is 4/8/12/14/16/20/24dp. Over the game, achievement pop-ups sit top centre, at most 400dp wide with 12dp sides, clear of the camera cutout and 8dp from the top (36dp when the speed pill shows, so they sit below it); the overlay pills follow 6dp under the card, 8dp apart, and the leaderboard counter pills sit in their own row 6dp under those, 8dp apart, three at most. Lists inside the pause panel (Achievements, Leaderboards, a board's score table, and the pause menu's own rows in the sideways right-hand column) scroll in at most 420dp, and a list longer than its room is cut partway through a row (never within 24dp of a row's edge, never on a divider), so it plainly goes on. A score table opens scrolled so the user's row shows whole with 24dp of the next row under it. The Settings column pads for the keyboard, so the sign-in fields stay above it. Touch targets are 48dp minimum (Continue 48dp, Resume and the first-start buttons 56dp, nav rows 64dp); text is in sp and is checked at font size 1.3x.

## Elevation & Depth

Depth is moulded and recessed, never floating. A raised piece (cart, settings group, pause panel) is drawn with the highlight colour showing as a 1dp line along its top edge and the shade colour as a 1.5dp lip below its bottom edge. A recessed piece (the slot bay) is the reverse: a black rim along the top, a faint highlight line at 50% along the bottom, and a darker mouth strip with a 10%-white lit edge across its top. Material's tonal elevation is flattened: surfaceTint is transparent, so containers differ by the plastic roles, not by tint. The only dimming is the scrim (black at 69%) behind menus over the game and under the pill overlays.

### Named Rules
**The Moulded Not Floating Rule.** Raised pieces show depth only through their 1dp highlight and 1.5dp shade lines in the plastic's own colours. No blurred drop shadows, gradients or glass.

**The Dark Slot Rule.** The slot is recessed and dark in both themes; it is where a cartridge goes in, never a generic card.

## Shapes

The signature silhouette is the cartridge: a body with 14dp corners and, along the top, a raised lip narrower than the body (inset 2.2 times the lip height on each side, its own corners 0.8 times the lip height). The lip height is 6dp on grid carts and the achievement pop-up, 9dp on the seated cart and empty-slot outline, and 10dp on the pause panel. Labels are 4dp-cornered printed stickers; the band rounds only its top corners. Save stickers are 6dp with 3dp pictures. Achievement badges are 4dp-cornered squares like a small label (52dp on the pop-up, 44dp in the list, 28dp in the overlay pills); a leaderboard's mark is the same 4dp square (52dp on the rank card, 40dp on a note). The user's row in a score table is a 4dp-cornered band of cart-high, and its rank sticker has 3dp print corners. Settings groups use 18dp corners, sheets and dialogs 22dp, the slot bay 20dp. Buttons are full pills like the console's Start and Select. Grip ridges are three 1dp lines 3dp apart, inset 22dp, 6dp above a cart's bottom. An empty slot is the cartridge silhouette dashed (1.5dp, white at 32%).

## Components

### Buttons
- **Shape:** full pill (rounded.pill).
- **Primary (accent):** accent fill with its on-colour; Continue has a play icon, 18dp start and 24dp end padding and label lettering at 17sp; Resume is full width at 56dp.
- **Outlined:** plastic with the outline stroke and ink text: "Open one game", Restart.
- **Danger:** Quit is a red-filled tonal pill; irreversible dialog actions are red text buttons.
- **States:** Material ripple and focus; disabled rows fade their icon, title and chevron to 38% while the summary stays readable. A pause-menu row that is off fades to 38% the same way and says why under its title in body small, soft ink, unfaded ("Off in hardcore mode").

### Chips (if used)
- **Style:** choice rows are Material segmented buttons: the chosen segment in the accent with its on-colour and no tick, unchosen segments on the plastic with the outline stroke and ink text; labels 13sp, weight 500.
- **State:** single choice; the colour alone shows the choice.

### Cards / Containers
- **Corner Style:** cartridges 14dp with the lip; settings groups 18dp.
- **Background:** cart (cart-grey / cart-smoked).
- **Shadow Strategy:** moulded edges only (see Elevation & Depth).
- **Border:** none; rows inside a group are divided by outlineVariant hairlines indented to the text (56dp with an icon).
- **Internal Padding:** cart 8dp sides (12dp seated, 14dp pause panel), top equals the lip plus three quarters of that.

### Inputs / Fields
- **Style:** Material 3 text fields and switches, themed: switches on are the accent; dialogs sit on the cart colour. The RetroAchievements sign-in is a settings group holding its help line first (body small, soft ink), then outlined Username and Password fields (the password with a show/hide eye, both with autofill hints), then the accent Sign in pill full width below the group; while it goes, the pill shows a small spinner in its own on-colour and reads "Signing in", and the fields are disabled. Signed in, the group shows a row with the trophy, the name and "Signed in to RetroAchievements", with an outlined Sign out pill below. The Hardcore group holds two switch rows, each with its help line in body small soft ink: "Hardcore mode", then "Leaderboard notes on screen" (on by default; it hides the started and failed notes and the counter pills, never the score card).
- **Error / Disabled:** errors in label red: the server's message under the fields, and "Your sign-in stopped working. Sign in again." above them. A page that couldn't load says so in upright body label red, with an outlined full-width "Try again" pill 14dp under it.

### Navigation
- **Style:** a header row (back arrow, label-lettered title with an optional body-small subtitle, icon buttons at the right). Settings is a list of nav rows (icon in soft ink, 500-weight title, summary, outline chevron) grouped on plastic cards. Sheets from the bottom open with the subject's label band as their title, then icon-and-label lines, danger lines last after a divider. Settings' main page has a RetroAchievements group: one trophy nav row whose summary says who is signed in and whether Hardcore is on. System Back always works.

### The Seated Cartridge and Slot (signature)
The last-played game sits in the slot bay: label band with the name and a menu button, the 3:2 picture of where it was left (sharp pixels), then a grip strip with the accent Continue pill and the time right-aligned. **The insert:** pressing Continue or the cart slides it up 64dp over 260ms (cubic-bezier 0.3, 0, 0.8, 0.15, accelerating) into the slot's mouth, clipped at the mouth's middle line while its label darkens to 60% black; then the game opens. A grid cart lifts 14dp, shrinks 4% and darkens the same way. With animations off, it is an instant cut.

### Game Cartridge
A grid cart: label band (name, two lines), a square label (box art fitted on label stock, else the picture cropped, else a blank label with a faint controller icon), and a grip strip with the day it was last played and a menu button. Tap plays; hold or the menu opens the game's sheet.

### Pause Panel
A cartridge panel (10dp lip) on the scrim over the frozen game, appearing with a 150ms fade and grow from 0.96. It holds the game's label band, the full-width Resume pill, flat menu rows (Save state, Load state, Achievements, Leaderboards, Cheats, Settings) divided by hairlines, the fast-forward speed choices, and Restart and Quit side by side at the bottom. Achievements appears only for a game with an achievement set, Leaderboards (a bar-chart icon) only for a game with boards. Sideways the rows take the right-hand column, which scrolls and is cut partway through a row on a short screen. In hardcore mode Load state and Cheats stay in place but are off: faded to 38% with "Off in hardcore mode" under them.

### Badge
An achievement's picture printed like a small label: the RetroAchievements badge on label stock, cropped square with 4dp corners. While there is no picture (not downloaded yet, or no internet), it is bare cart-high plastic with a trophy in soft ink at 55% of its size. Locked achievements show the set's own grey badge; the app does not tint or fade it.

### Achievement Pop-up
A small moulded cartridge over the game (6dp lip, 12dp sides, the grip ridges in a 14dp strip at the bottom) that drops in from above, one at a time: a 220ms slide from half its height up with a 160ms fade, leaving with a 200ms slide and fade upward; it takes no touches, so the touch buttons under it keep working. An unlock uses the 52dp badge as its label, the achievement's name in title-small label lettering (two lines at most), "Unlocked · " and its description in body small soft ink, and the points at the right. A mastered set puts the game's label band across the top, then "Mastered: every achievement unlocked" in body and the points. A server problem's title is upright Roboto 500 in label red, its detail in body small soft ink; a note is one body line. TalkBack reads each card once as one sentence.

Leaderboard cards are the same cart with the board's mark as their label: bare cart-high plastic with the board icon in soft ink at 55%, 4dp corners (40dp on a note, 52dp on the rank card). The board's name is in title-small label lettering (two lines at most) and what happened goes under it in body small soft ink with tabular figures: "Leaderboard started", "Leaderboard failed", "Score X · sending". The rank card splits its line into "Score X" and "N players · best Y" (the best only when it differs), with no-break spaces so a number never parts from its word and no "·" is left at a line's end, and stamps the rank at the right like points: "#N" in title lettering with a "rank" tag under it, in ink, never the accent. A score that couldn't be sent is a problem card: "Couldn't send a score" upright in label red, the board and reason under it. **Timings:** started and failed notes show 2 s, other notes 3 s, an unlock and the rank card 4 s, mastered 6 s. **The replace rule:** a board's newer news takes its older card's place (the rank replaces its "sending" card, a failed note its started note), shown at once for its own time if the older one is on screen, else in the older one's place in the queue.

### Overlay Pills
Under the pop-up, on the scrim like the speed pill: one pill holding the 28dp badges of the challenges going on, and apart from it one pill with the latest progress badge and its count in white tag lettering with tabular figures. Badge pills are full rounds with 4dp padding. Below them, one counter pill per running leaderboard attempt (three at most): the board icon (16dp) and the counter's text (a time or a score) in white tag lettering with tabular figures, 36dp tall like the badge pills, 10dp padding before the icon and 12dp after the text. The Leaderboard notes switch hides them.

### Points
An achievement's points printed in title lettering with tabular figures and a "pts" tag under it in soft ink. Points are ink when unlocked and soft ink when locked, never the accent: they cannot be pressed.

### Achievements Panel
A page of the pause panel: the header "Achievements" with the game as its subtitle, a summary line in body soft ink with tabular figures ("12 of 40 unlocked · 85 of 400 points", plus "· Hardcore"), and a share bar: a 6dp full-round track in the shade colour with an ink fill for the unlocked share. Then every achievement, unlocked first, in rows divided by hairlines: the 44dp badge, the name in title small (soft ink while locked), the description in body small soft ink (three lines at most), the progress as a tag for locked ones that count, and the points at the right.

### Leaderboards Panel
A page of the pause panel like Achievements: the header "Leaderboards" with the game as its subtitle and, while Hardcore is off, one line in body soft ink: "Scores are only sent in Hardcore mode". Then the boards in 64dp rows divided by hairlines: the name in title small, its description in body small soft ink (two lines at most), an outline chevron. An opened board's header is its name with its description as the subtitle. While loading: a 32dp spinner in soft ink. Couldn't load (offline or refused): the message in label red and the outlined "Try again" pill. An empty board: "No scores yet" in body soft ink.

**The score table.** A line in body soft ink, tabular: "N players", plus "· You haven't set a score yet" when the user isn't on the board. Then rows divided by hairlines (44dp, 8dp padding): the rank in title-small label lettering, tabular, in a 64dp column (10dp to the name); the name in body; the score right-aligned, tabular. The user's row is a 4dp band of cart-high with the name bold and the rank printed on a small label-stock sticker in day ink (3dp corners), so it stands out on smoked plastic too without the accent. The sticker reaches 6dp left into the row's padding so its figures line up with the ranks above and below; by day it has a 1dp outline-variant edge, where label stock and the cart-high band are close. Missing places are a quiet centred tag between hairlines ("Ranks 11 to 14", or "Rank 11"), never an icon. Rows cannot be pressed.

### Hardcore Restart Dialog
Switching Hardcore on during a game with achievements asks "Restart the game?" in a standard dialog on the cart colour, explaining that hardcore starts from the title screen and unsaved progress is lost. Restart is a label-red text button; Cancel is a text button in the accent (it can be pressed).

### About and Document Pages
Settings → About DreamBoy (its own group at the end of Settings, info icon, "Version 1.0.0" as the summary): a settings group with the version in tabular body small soft ink, what the app is in body, and the licence line in body small soft ink — no label band (the header names the app); then a group of nav rows: GitHub page (the code icon; its trailing icon is open-in-new, not the chevron, because it leaves the app), Open-source licences, Privacy policy. Licences is a group of text-only nav rows (part, licence as the summary); a licence's page has the header "Licence" with the part as its subtitle and the text, paragraphs reflowed, in a group. The privacy policy is a document page: document headings, body paragraphs, soft-ink bullets with bold runs, the dated note in body small soft ink, web addresses as underlined accent links that open the browser. These reading pages are centred and at most 600dp wide, so lines stay readable sideways.

### Save Sticker
A filled slot is printed label stock with the picture, the date and time, and the slot number in 84sp label lettering in day ink. An empty slot is bare cart-high plastic with "Empty" as a tag and the number in the theme's ink. The quick slot shows "Q".

## Do's and Don'ts

### Do:
- **Do** derive every accent from the chosen skin's seed through the lightness loops, and check the result against the 4.5:1 floor in both themes.
- **Do** put a game's name on a black label band in white Roboto Black Italic wherever the game itself is the subject (carts, the game sheet, the pause panel).
- **Do** build new raised containers from the cart colour with the moulded highlight and shade, and new recessed areas with the slot's recess.
- **Do** keep label stock and its day inks identical in both themes, and print it only where something is printed.
- **Do** use standard Material 3 components with the themed colours, pill buttons and the app's shapes.
- **Do** use tabular figures for times, dates, counts, scores and ranks.
- **Do** show a feature that is off for a reason by fading its row to 38% in place and saying why under it, rather than hiding it.

### Don't:
- **Don't** use the accent on anything that cannot be pressed or switched on.
- **Don't** use label red for anything except danger and errors.
- **Don't** add blurred drop shadows, gradients or glass; depth is moulding and recessing only.
- **Don't** bundle or download a font; use the phone's Roboto, Roboto Black Italic and Roboto Condensed.
- **Don't** print Nintendo's logos, the "GAME BOY ADVANCE" logotype or the seal, on screens or in the icon.
- **Don't** make the slot light in the light theme, or put an empty save slot on label stock.
- **Don't** use Roboto Black Italic for running text, help notes or settings rows.
