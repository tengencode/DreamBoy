# Milestone 2 — Graphics (PPU), DMA, Timers, PC Viewer — Design

**Date:** 2026-09-24
**Status:** Implemented — milestone complete 2026-09-25 (tag `milestone-2`)
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§4.1 PPU, §4.3 DMA, §4.4 timers, §9 testing, §10 milestones)
**Builds on:** milestone 1 (tag `milestone-1`): CPU, bus, HLE BIOS, scheduler, LCD timing, headless test runner.

---

## 1. Goal and scope

Produce a correct 240×160 picture every frame, plus the DMA copier and timers that real games and demos need to display anything, and PC tools to see and verify the picture.

| In scope | Out of scope (milestone) |
|---|---|
| PPU rendering: modes 0–5, text + affine BGs, bitmap modes, sprites (regular/affine/double-size, 1D/2D mapping, semi-transparent, OBJ window), windows, blending (alpha/brighten/darken), mosaic, forced blank, per-line OBJ cycle budget | Mid-scanline register changes; green-swap (undocumented) |
| DMA 0–3: immediate, VBlank, HBlank; all address modes; repeat; IRQ | Sound-FIFO DMA timing (M3, with sound); DMA3 video capture (stubbed, never fires) |
| Timers 0–3: prescalers, cascade, overflow IRQ, lazy counting | Timer→sound FIFO link (M3); 2-cycle start delays (M3 timing suite) |
| Test runner: `--frames`, `--screenshot`, `--hash`, `--expect-hash` | Audio output |
| `gbaview.exe` Win32 viewer (live play, keyboard, F12 screenshot) | Gamepad input on PC |
| ROM fetch: jsmolka `ppu` ROMs + tonc demo binaries | Committing any ROM or screenshot |

**Change to the parent plan (approved):** DMA and timers move from milestone 3 into milestone 2. Milestone 3 becomes: sound (APU + FIFO DMA), save chips + GPIO, prefetch buffer, open-bus/I-O read accuracy, timing-suite fixes.

---

## 2. Files

```
core/include/gba.h            + const uint16_t *gba_framebuffer(const gba_t *g);  /* 240x160 BGR555 */
core/src/ppu/ppu.h / ppu.c    timing (exists) + framebuffer, affine reference latches, line dispatch
core/src/ppu/ppu_internal.h   per-line layer buffer types shared by the drawing files
core/src/ppu/ppu_bg.c         text, affine and bitmap background line renderers
core/src/ppu/ppu_obj.c        sprite line renderer
core/src/ppu/ppu_compose.c    windows, priority, blending → framebuffer line
core/src/sys/dma.h / dma.c    DMA channels
core/src/sys/timer.h / timer.c
core/src/sys/sched.h          + EV_TIMER0..3, EV_DMA
core/src/mem/io.c             + dispatch for PPU affine latches, DMA and timer registers
tools/common/png.{h,c}        PNG writer (stored deflate blocks, CRC32, Adler-32; no zlib)
tools/testrunner/main.c       + --frames/--screenshot/--hash/--expect-hash
tools/viewer/main.c           gbaview.exe (Win32 GDI)
tools/fetch_test_roms.py      + jsmolka ppu ROMs, tonc-bin.zip (selected demos extracted)
tests/expected_frames.json    approved frame fingerprints
```

---

## 3. PPU

### 3.1 Registers and state

- The I/O backing store (`io.raw`) already keeps every written value. The renderer reads BG control, scroll, affine parameters, window, mosaic and blend registers from it directly (little-endian 16-bit reads by offset).
- Dedicated state in `ppu_t`: `dispcnt`, `dispstat`, `vcount` (as in M1), the 240×160 framebuffer, and the **internal affine reference points** for BG2 and BG3 (`ref_x`, `ref_y`, 28-bit signed 20.8 fixed point).
- Writes to BG2X/BG2Y/BG3X/BG3Y (0x028–0x02F, 0x038–0x03F) call `ppu_affine_ref_written()`, which reloads the matching internal reference immediately.
- Register map used (offsets from 0x04000000): DISPCNT 000, DISPSTAT 004, VCOUNT 006, BG0–3CNT 008–00E, BG0–3HOFS/VOFS 010–01E, BG2PA–PD 020–026, BG2X 028, BG2Y 02C, BG3PA–PD 030–036, BG3X 038, BG3Y 03C, WIN0H 040, WIN1H 042, WIN0V 044, WIN1V 046, WININ 048, WINOUT 04A, MOSAIC 04C, BLDCNT 050, BLDALPHA 052, BLDY 054.
- Write-only registers still read back the stored value (hardware returns open bus); correcting reads is M3 work.

### 3.2 When lines are drawn

- At the HBlank event of lines 0–159: draw that line into the framebuffer, then trigger HBlank DMA.
- At VBlank start (line 160): reload both affine references from BGxX/BGxY; trigger VBlank DMA.
- After each drawn line: `ref_x += PB`, `ref_y += PD` for BG2 and BG3.
- DISPCNT bit 7 (forced blank): the line is white (0x7FFF).

### 3.3 Layer line buffers

Each layer renders 240 entries of `{ uint16_t color; uint8_t prio; uint8_t flags }`, with flags for *transparent*, and for OBJ: *semi-transparent* and *in OBJ window*. Unused layers stay fully transparent.

### 3.4 Backgrounds

- **Text (modes 0, 1):** BGxCNT gives priority, char base (×16 KiB), 4bpp/8bpp, mosaic, screen base (×2 KiB), size (256×256, 512×256, 256×512, 512×512). Tiles are 8×8 with H/V flip and a palette bank in 4bpp. Colour index 0 is transparent. Scroll 9 bits with wraparound.
- **Affine (BG2 in mode 1; BG2 and BG3 in mode 2):** map sizes 128/256/512/1024, 8bpp tiles, 1-byte map entries. Per pixel, texture position = ref + x·(PA, PC). Outside the map: wrap if BGxCNT bit 13, otherwise transparent.
- **Bitmap (BG2 in modes 3/4/5):** sampled through the same affine path. Mode 3: 240×160 BGR555. Mode 4: 240×160 8-bit palette indices, page at 0xA000 when DISPCNT bit 4. Mode 5: 160×128 BGR555, page at 0xA000. Outside the bitmap is transparent; mode 4 index 0 is transparent.
- **Mosaic:** MOSAIC holds BG and OBJ block sizes (1–16). BG mosaic snaps the sample x to a multiple of H-size and the line to a multiple of V-size.
- Which BGs exist per mode: 0 = BG0–3 text; 1 = BG0–1 text + BG2 affine; 2 = BG2–3 affine; 3/4/5 = BG2 bitmap. Modes 6–7 draw only the backdrop.

### 3.5 Sprites

- 128 OAM entries (8 bytes: attr0/1/2), 32 affine parameter sets (PA/PB/PC/PD in the 4th halfword of each group of 4 entries).
- Attr0: Y, affine flag, double-size / disable flag, mode (normal, semi-transparent, OBJ window), mosaic, 4/8bpp, shape. Attr1: X (9-bit signed), affine index or H/V flip, size. Attr2: tile number, priority, palette bank.
- Sizes: square 8/16/32/64, wide 16×8/32×8/32×16/64×32, tall 8×16/8×32/16×32/32×64.
- Tile mapping: DISPCNT bit 6 = 1D (tiles consecutive), else 2D (32-tile rows). OBJ tiles start at VRAM 0x10000; in bitmap modes tile numbers below 512 are not displayed.
- Palette: OBJ palette at palette RAM 0x200.
- Affine sprites: sample with the parameter set around the sprite centre; the double-size flag doubles the bounding box without scaling.
- Ordering: among sprites, lower OAM index wins; each pixel keeps the first opaque sprite pixel found in index order. OBJ-window sprites only mark the window mask.
- **Per-line budget:** 1210 cycles (954 when DISPCNT bit 5, H-Blank Interval Free, is set). Normal sprite = width cycles; affine = 2·width + 10. Sprites past the budget are not drawn on that line.
- OBJ mosaic applies horizontally and vertically using the OBJ block sizes.

### 3.6 Compositing

- **Window mask per pixel:** WIN0 wins over WIN1, which wins over the OBJ window, which wins over outside (WINOUT low byte). WIN0H/WIN1H/WIN0V/WIN1V give right/bottom exclusive bounds; if left > right (or top > bottom) the window wraps. Windows apply only when enabled in DISPCNT bits 13–15; when none are enabled, everything is visible and effects are allowed.
- **Top two layers:** order by priority (0 = front); on a tie OBJ is in front of BGs, then BG0 < BG1 < BG2 < BG3. The backdrop (palette entry 0) is always the bottom-most layer.
- **Effects (BLDCNT):** 1st-target and 2nd-target layer bits (BG0–3, OBJ, backdrop) and mode: none / alpha / brighten / darken.
  - Alpha: `min(31, (a·EVA + b·EVB) >> 4)` per 5-bit channel, EVA/EVB capped at 16.
  - Brighten: `c + ((31 − c)·EVY >> 4)`. Darken: `c − (c·EVY >> 4)`; EVY capped at 16.
  - A semi-transparent sprite on top forces alpha blending with the layer beneath when that layer is a 2nd target, whatever the BLDCNT mode.
  - The window's effect-enable bit gates all effects at that pixel.

---

## 4. DMA

- Registers per channel n (base 0x0B0 + 12n): SAD (32), DAD (32), CNT_L (count, 16), CNT_H (control, 16).
- CNT_H: dest control (bits 5–6: inc, dec, fixed, inc+reload), source control (7–8: inc, dec, fixed; 3 treated as inc), repeat (9), 32-bit (10), start timing (12–13: immediate, VBlank, HBlank, special), IRQ (14), enable (15).
- On an enable 0→1 write: latch source/destination (masks: DMA0 source 0x07FFFFFF, DMA1–2 source 0x0FFFFFFF, DMA0–2 destination 0x07FFFFFF, DMA3 both 0x0FFFFFFF) and count (0 → 0x4000 for DMA0–2, 0x10000 for DMA3). Immediate timing schedules `EV_DMA` for right after the enabling instruction (the CPU cannot get further before the DMA takes the bus); the 2-cycle start delay is charged as idle time. The frame loop re-reads the next event time after every instruction, so events scheduled by I/O writes are never late.
- Triggers: `dma_on_vblank()` and `dma_on_hblank()` (lines 0–159 only) run every enabled channel with that timing, lowest channel first. Special timing: DMA1/2 are left for sound (M3); DMA3 video capture never fires.
- Transfer: units aligned to their size; first access non-sequential, then sequential. The CPU is stalled: bus cycles simply advance `g->now`. Reads below 0x02000000 return the DMA latch (the last value transferred).
- End: IRQ if enabled (IRQ_DMA0 << n). Repeat (and not immediate): reload count, reload destination if dest control = 3, stay enabled. Otherwise clear the enable bit.

## 5. Timers

- Registers per timer n (base 0x100 + 4n): CNT_L (read: current counter; write: reload value), CNT_H (prescaler 0–3 → 1/64/256/1024, count-up bit 2 (ignored for timer 0), IRQ bit 6, enable bit 7).
- Enabling (0→1): counter = reload, start time = now. Non-cascade timers schedule `EV_TIMERn` at the overflow cycle: `start + (0x10000 − counter) × prescale`.
- Reading a running non-cascade timer: `counter + (now − start) / prescale`, truncated to 16 bits.
- Overflow: counter = reload, raise IRQ_TIMERn if enabled, tick timer n+1 if it is enabled in count-up mode (which may overflow in turn), reschedule.
- Writing CNT_H of a running timer with new prescaler: capture the current count, restart timing from now. Disabling: capture the count, cancel the event.

---

## 6. PC tools

- **PNG writer** (`tools/common/png.c`): `png_write_bgr555(path, pixels, width, height)`; RGB888 via `c5 << 3 | c5 >> 2`; zlib stream of stored blocks; CRC32 and Adler-32 computed in place.
- **Frame fingerprint:** 64-bit FNV-1a over the 240×160 framebuffer bytes, printed as 16 hex digits.
- **Test runner:** `--frames N` runs exactly N frames (no idle-loop wait unless `--result-reg` is given). `--screenshot file.png` saves the final frame. `--hash` prints `HASH <hex>`. `--expect-hash <hex>` exits 0 on match, 1 otherwise.
- **gbaview.exe** (built only on Windows): `gbaview <rom.gba> [--bios file]`. 3× window, resizable, nearest-neighbour `StretchDIBits`. Paced to 59.7275 fps with a high-resolution timer. Keys: arrows, Z = A, X = B, A = L, S = R, Enter = Start, Backspace = Select, F12 = screenshot (`gbaview-<n>.png`), Esc = quit.
- **Fetch script:** adds jsmolka `ppu/hello.gba`, `ppu/shades.gba`, `ppu/stripes.gba` (same pinned commit) and `tonc-bin.zip` from coranac.com (about 0.9 MB), extracting only the chosen demos into `tests/roms/tonc/`. Asks the user before downloading.

---

## 7. Testing

1. **Scene unit tests (pixel-exact):** write VRAM/palette/OAM/registers directly, call the line renderer, and assert exact framebuffer pixels. At least one test per feature in §3: backdrop, forced blank, text BG 4bpp/8bpp, scroll + wrap, flips, BG priority, affine BG (identity, scale, wrap and no wrap), modes 3/4/5 + page select, sprites (regular, flipped, affine, double-size, 1D vs 2D, 8bpp, priority vs BG, OAM order), OBJ budget, windows (inside/outside/wrap/OBJ window), alpha/brighten/darken, semi-transparent OBJ, BG and OBJ mosaic, and affine reference latch/step.
2. **DMA tests:** immediate word/halfword copy, each address mode, count 0, VBlank and HBlank triggers (HBlank only on visible lines), repeat with destination reload, IRQ, and the disable-after-transfer rule.
3. **Timer tests:** counter reads over time at each prescaler, overflow IRQ timing, reload behaviour, cascade, stop/restart.
4. **Approved screenshots:** each test ROM and about 12 tonc demos (chosen in the plan to cover every feature) run for a fixed number of frames. Claude inspects each PNG against the demo's description in the Tonc guide; once correct, the fingerprint goes into `tests/expected_frames.json` and CTest runs `--expect-hash`. Missing ROMs → skipped (exit 77).
5. Memory-error checkers (ASan + UBSan) on for every host test run.

## 8. Done when

- All unit tests pass (milestone 1's included).
- jsmolka `hello`, `shades`, `stripes` and the chosen tonc demos match their approved fingerprints.
- `gbaview` runs a tonc demo live at full speed with working keys.
- Sanitizers report nothing.
