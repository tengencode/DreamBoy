# DreamBoy (GBA emulator for Android) — Design Spec

**Date:** 2026-09-24
**Status:** Approved design; implemented milestone by milestone (milestones 1-7 done — 7 split into 7a and 7b; milestone 8 (app polish + skins) added 2026-10-03: 8a and 8b done — milestone 8 complete 2026-10-04; milestones 9 (button pictures + skin import — done as Delta skins 2026-10-04, spec `2026-10-04-milestone-9-delta-skins-design.md`) and 10 (cheat-file import, cloud sync, box art; split 2026-10-04 into 10a cheat files (done 2026-10-04, spec `2026-10-04-milestone-10a-cheat-files-design.md`), 10b box art (done 2026-10-04, spec `2026-10-04-milestone-10b-box-art-design.md`), 10c cloud sync — cancelled by the user 2026-10-04 before any code, spec `2026-10-04-milestone-10c-cloud-sync-design.md` kept as a record; milestone 10 complete); milestone 11 (backup and restore: one `.zip` the user keeps, restore with a safety copy, optional automatic backups to a picked folder, Android's own backup off) added 2026-10-04 and done the same day (tag `milestone-11`), spec `2026-10-04-milestone-11-backup-design.md` added 2026-10-04; milestone 12 (the UI overhaul "Game Pak": a new look for every screen, the app icon and launch screen, shaped with the impeccable skill) added 2026-10-05, done the same day (tag `milestone-12`; impeccable finish review `ship`, `DESIGN.md` written, the user's check passed), spec `2026-10-05-milestone-12-game-pak-ui-design.md`; milestone 13 (the game check: test many games, fix the real problems) and milestone 14 (RetroAchievements — the one exception to the no-sign-ins rule) added 2026-10-05; 13 done the same day (tag `milestone-13`: 30 games locked in, the built-in BIOS's MidiKey2Freq, BitUnPack and affine sets fixed and twin-tested against the real BIOS), spec `2026-10-05-milestone-13-game-check-design.md`; 14 done 2026-10-06 (tag `milestone-14`: rcheevos 12.5.0, sign-in, unlock pop-ups, the Achievements list, Hardcore — off by default until RetroAchievements approves the app; a real unlock on the user's account), spec `2026-10-06-milestone-14-retroachievements-design.md`; milestone 15 (RetroAchievements leaderboards) added and done 2026-10-06 (tag `milestone-15`), spec `2026-10-06-milestone-15-leaderboards-design.md`; no milestone planned after 15; the user decided 2026-10-23 that the app will be released free and open source, under GPL-3.0-or-later (2026-10-24; the public repo starts that day with one clean commit; milestone 16 (release prep) done 2026-11-12: the app is **DreamBoy**, public on GitHub as `tengencode/DreamBoy` with release v1.0.0; spec `2026-11-12-milestone-16-release-prep-design.md`); milestone 17 (Game Boy and Game Boy Color in the same app, our own second core) added 2026-11-12, spec `2026-11-12-milestone-17-game-boy-design.md`, in progress); each milestone has its own spec and plan — see §10)
**Goal:** A Game Boy Advance emulator for Android comparable to Pizza Boy GBA: high compatibility, low latency, save states, rewind, fast-forward, cheats, gamepads, sensors, and customizable touch skins.

---

## 1. Decisions Summary

| Topic | Decision |
|---|---|
| Languages | Emulator core, rendering, audio, input loop, sensors: **C11 + ARM64 assembly**. App UI: **thin Kotlin shell** (Jetpack Compose). |
| Graphics API | **OpenGL ES 3.0** via EGL on an `ANativeWindow` |
| Audio API | **AAudio** (shared, low-latency output stream; changed from OpenSL ES by user decision 2026-09-26, milestone 4b — OpenSL ES is deprecated) |
| CPU strategy | **Cached interpreter** (predecoded blocks) with ARM64 asm dispatch loop and ALU handlers; dynarec possible later behind the same interface |
| BIOS | **Built-in HLE BIOS** by default; optional user-supplied real BIOS |
| Accuracy | **High compatibility**: scanline renderer, cycle-counted instructions, wait states, prefetch buffer, timer/DMA timing (mGBA / Pizza Boy tier) |
| Targets | **arm64-v8a** (asm paths) and **x86_64** (C fallbacks, for emulator testing). **minSdk 26** (Android 8.0) |
| Runtime model | Dedicated **emulation thread**, **audio-synced** with dynamic rate control; separate **render thread** on Choreographer vsync; **triple-buffered** frames |
| Host testing | **LLVM clang on Windows** + SDK-bundled CMake 3.31/Ninja; headless test runner |
| v1 features | Save states, fast-forward, rewind, cheats, gamepads, rumble, tilt/gyro/solar sensors, RTC, custom skins + layout editor |
| Out of scope (v1) | Link cable / multiplayer, cloud sync, box art / network, 32-bit ARM, per-pixel (cycle-accurate) rendering, DMA3 video capture, dynarec. (2026-10-04, user decision: cloud sync and box art moved into milestone 10, after v1; later that day the user cancelled cloud sync (10c): saves stay local and users back them up themselves — fewer integrations and app keys for licensing and audits.) |

---

## 2. Architecture & Repository Layout

```
gba_emulator/
├── core/                    # Pure C11 + ARM64 asm. Depends only on libc.
│   ├── include/gba.h        # The ONLY public core API
│   ├── src/
│   │   ├── cpu/             # ARM7TDMI: decode tables, ARM + Thumb handlers, block cache
│   │   ├── mem/             # Bus, region table, wait states, prefetch, I/O dispatch
│   │   ├── ppu/             # Scanline renderer, modes 0–5, OBJ, windows, blending
│   │   ├── apu/             # 4 PSG channels + 2 DMA FIFOs, mixer
│   │   ├── sys/             # Scheduler, DMA, timers, IRQ, keypad, SIO stub
│   │   ├── cart/            # ROM, SRAM/Flash/EEPROM, GPIO (RTC/solar/gyro/rumble), tilt, game DB
│   │   ├── bios/            # HLE BIOS (stub image + C SWIs) and real-BIOS loader
│   │   ├── state/           # Save-state serialization, rewind ring buffer
│   │   └── cheats/          # GameShark / Action Replay / CodeBreaker / raw decoders
│   ├── third_party/lz4/     # Vendored LZ4 (BSD)
│   └── arch/
│       ├── arm64/*.S        # Assembly: dispatch loop, ALU/flags, pixel, XOR kernels
│       └── generic/*.c      # C twin of every asm routine
├── platform/android/        # C: EGL/GLES3, AAudio, threads, sensors, resampler, JNI
│   └── arch/{arm64,generic} # NEON resampler kernel + C twin
├── app/                     # Kotlin shell: library, settings, game screen, overlay, skin editor
├── tools/testrunner/        # Host CLI: runs ROMs headless, hashes/dumps frames & audio
└── tests/
    ├── unit/                # C unit tests (tiny in-house harness)
    ├── expected.json        # Test-ROM → expected framebuffer/audio hashes
    ├── replays/             # Input-replay regression files
    └── roms/                # gitignored; populated by tools/fetch_test_roms.py
```

### 2.1 Layering rules

- `core/` has **no** Android, OS, threading, or file I/O dependencies. Heap allocation happens only in `gba_create`, `gba_load_rom` (ROM copy + save RAM), and `gba_rewind_configure` (rewind ring) — never inside `gba_run_frame`.
- The core is **pull-based**: the frontend calls `gba_run_frame`, then reads video/audio. No callbacks out of the core.
- Sensors and RTC time are **inputs** the frontend sets; rumble is an **output** the frontend polls.
- `platform/android/` owns all threads and OS APIs. Threads communicate via lock-free SPSC structures and atomics.
- Kotlin ↔ native crosses JNI only for control (start/pause/settings/state ops/key mask), never per-pixel or per-sample.
- The same `core/` builds for: NDK arm64 (asm), NDK x86_64 (generic C), host Windows x86_64 (generic C). Build flag `GBA_FORCE_GENERIC` forces generic C on arm64 for equivalence testing.

### 2.2 Public core API (`core/include/gba.h`)

```c
typedef struct gba gba_t;

typedef struct {
    uint32_t flags;             /* e.g. GBA_CFG_SKIP_BIOS_INTRO */
} gba_config_t;

typedef enum { GBA_OK = 0, GBA_ERR_ROM_SIZE, GBA_ERR_BIOS_SIZE,
               GBA_ERR_STATE_MAGIC, GBA_ERR_STATE_VERSION,
               GBA_ERR_STATE_ROM_MISMATCH, GBA_ERR_STATE_CORRUPT,
               GBA_ERR_CHEAT_FORMAT, GBA_ERR_NOMEM } gba_err_t;

gba_t*          gba_create(const gba_config_t* cfg);
void            gba_destroy(gba_t* g);
gba_err_t       gba_load_rom(gba_t* g, const uint8_t* rom, size_t size);
gba_err_t       gba_load_bios(gba_t* g, const uint8_t* bios, size_t size); /* 16 KiB; omit → HLE */
void            gba_reset(gba_t* g);

void            gba_run_frame(gba_t* g);                  /* exactly one frame (280,896 cycles) */
const uint16_t* gba_framebuffer(const gba_t* g);          /* 240x160, BGR555 */
size_t          gba_audio_read(gba_t* g, int16_t* out, size_t max_frames); /* stereo @ 32768 Hz */

void            gba_set_keys(gba_t* g, uint16_t pressed_mask);
void            gba_set_tilt(gba_t* g, int16_t x, int16_t y);
void            gba_set_gyro(gba_t* g, int16_t z_rate);
void            gba_set_light(gba_t* g, uint8_t level);  /* 0 = dark .. 255 = max sun */
void            gba_set_rtc(gba_t* g, int64_t unix_seconds, int32_t utc_offset_s);
int             gba_rumble_active(const gba_t* g);

/* Save RAM */
size_t          gba_saveram_size(const gba_t* g);
uint8_t*        gba_saveram(gba_t* g);
int             gba_saveram_dirty(gba_t* g);  /* returns and clears dirty flag */

/* Save states */
size_t          gba_state_size(const gba_t* g);
size_t          gba_state_save(const gba_t* g, void* buf, size_t cap);
gba_err_t       gba_state_load(gba_t* g, const void* buf, size_t len);

/* Rewind */
void            gba_rewind_configure(gba_t* g, size_t budget_bytes, int interval_frames);
int             gba_rewind_step_back(gba_t* g);  /* returns 0 when history exhausted */

/* Cheats */
gba_err_t       gba_cheat_add(gba_t* g, const char* code_text, int* out_id);
void            gba_cheat_enable(gba_t* g, int id, int enabled);
void            gba_cheat_clear(gba_t* g);

/* Info */
const char*     gba_rom_title(const gba_t* g);   /* 12-char header title */
const char*     gba_rom_code(const gba_t* g);    /* 4-char game code */
uint32_t        gba_rom_crc32(const gba_t* g);
```

When rewind is configured with a non-zero budget, `gba_run_frame` captures a rewind snapshot automatically every `interval_frames` frames; the frontend only calls `gba_rewind_step_back` while the rewind button is held. A budget of 0 disables rewind and frees the ring.

---

## 3. Core Internals

### 3.1 Scheduler

- Master clock: 16.777216 MHz, 64-bit cycle counter.
- Binary min-heap of events keyed by absolute timestamp. Event kinds: HDraw end, HBlank end, timer overflow (×4), DMA start, APU frame sequencer (512 Hz), APU sample tick, FIFO timer tick, IRQ-delay, halt wake.
- CPU executes until `min(next_event.timestamp)`; the scheduler then dispatches due events.
- `gba_run_frame` runs until the VBlank-start event has fired once (1 frame = 228 lines × 1232 cycles = 280,896 cycles).
- Timers are **lazy**: counter value is computed on read from start timestamp and prescaler; only overflow is scheduled.
- Events are identified by enum IDs (not function pointers) so the heap serializes into save states.

### 3.2 CPU — ARM7TDMI cached interpreter

- **Decode tables:** ARM: 4096 entries indexed by `bits[27:20] << 4 | bits[7:4]`. Thumb: 1024 entries indexed by `bits[15:6]`.
- **Decoded-block cache:** covers ROM, IWRAM, EWRAM. A decoded entry = handler pointer + pre-extracted operand fields + base cycle cost. Blocks terminate at branches, PC writes, SWI, and mode changes.
- **Invalidation:** ROM blocks are never invalidated (except by cheat ROM patches, which invalidate the affected page). IWRAM/EWRAM pages carry a "contains code" bit; a write to such a page invalidates that page's blocks.
- **Code outside cached regions** (BIOS, VRAM, etc.) runs through the uncached decode path.
- **Condition codes** checked per instruction (ARM); flags stored as separate N/Z/C/V fields in generic C, and in host NZCV within the asm path (materialized to the struct on exit from the dispatch loop).
- **Modes:** User, FIQ, IRQ, Supervisor, Abort, Undefined, System, with full register banking and SPSRs.
- **Exceptions:** Reset, Undefined, SWI, IRQ (FIQ and Abort not raised by GBA hardware but modeled for mode-switch correctness).
- **Halt:** `HALTCNT` write → CPU idle; scheduler fast-forwards to next event; wake when `(IE & IF) != 0`.
- **Cycle accounting:** each access charges wait states from the memory bus (N/S, width, region); internal cycles (I) per instruction class per ARM7TDMI timing tables.

**ARM64 assembly (`arch/arm64/`)**
- Dispatch loop: `x19` = core pointer, `x20` = guest PC, `x21` = cycles remaining, `x22` = current block pointer.
- Data-processing and shifter handlers use native `ADDS/SUBS/ADCS/SBCS/ANDS` etc. and `MRS/MSR NZCV` to obtain guest flags.
- Every asm routine has a generic C twin with identical semantics.

### 3.3 Memory bus

- **Region table:** 16 entries indexed by `addr >> 24`, each with `{base pointer, mirror mask, wait-state indices, handler-or-NULL}`. Direct-mapped regions (BIOS, EWRAM, IWRAM, palette, VRAM, OAM, ROM) take an inline fast path; I/O and cart save spaces go through handlers.
- **Region quirks:**
  - EWRAM 256 KiB mirrored across 0x02xxxxxx; IWRAM 32 KiB mirrored across 0x03xxxxxx.
  - VRAM: 96 KiB within a 128 KiB window; 0x06018000–0x0601FFFF mirrors 0x06010000–0x06017FFF.
  - 8-bit writes: palette and BG VRAM write the byte to both halves of the halfword; OBJ VRAM and OAM ignore 8-bit writes.
  - Open bus: unmapped reads return the last prefetched opcode value (ARM/Thumb-state rules).
  - BIOS read protection: reads from BIOS while PC is outside BIOS return the last successfully fetched BIOS opcode.
  - ROM reads past end of image return `(addr >> 1) & 0xFFFF` pattern.
- **Wait states:** from `WAITCNT` for SRAM and the three ROM wait-state regions, per access width, N vs S. Fixed timings for EWRAM (3/3/6 for 8/16/32-bit), IWRAM/palette/VRAM/OAM (1 cycle, +1 for 32-bit on 16-bit buses).
- **Prefetch buffer:** modeled on mGBA's approach (8-halfword buffer fed during non-ROM cycles when `WAITCNT` bit 14 is set).

### 3.4 BIOS

- **HLE mode (default):**
  - A built-in ~200-byte BIOS stub (real ARM code, stored as a C byte array) provides the Reset, SWI, and IRQ vectors.
  - The stub IRQ handler matches the real BIOS: push `r0–r3, r12, lr`, set return address, call `[0x03FFFFFC]`, restore, `subs pc, lr, #4`.
  - The stub SWI vector executes a reserved trap instruction that the CPU routes to the C HLE dispatcher, then returns via `movs pc, lr`.
  - Implemented SWIs: SoftReset, RegisterRamReset, Halt, Stop, IntrWait, VBlankIntrWait, Div, DivArm, Sqrt, ArcTan, ArcTan2, CpuSet, CpuFastSet, GetBiosChecksum, BgAffineSet, ObjAffineSet, BitUnPack, LZ77UnCompWram/Vram, HuffUnComp, RLUnCompWram/Vram, Diff8bitUnFilterWram, Diff8bitUnFilterVram, Diff16bitUnFilter, SoundBias. Sound-driver SWIs (MusicPlayer*, SoundDriver*) are no-op stubs.
  - Boot: skip directly to `0x08000000` in System mode with post-BIOS register state (SP_svc=0x03007FE0, SP_irq=0x03007FA0, SP_sys=0x03007F00, `POSTFLG`=1, etc.).
- **Real BIOS mode:** user-supplied 16 KiB `gba_bios.bin` is mapped at 0x00000000 and executed natively, including boot logo (skippable via `GBA_CFG_SKIP_BIOS_INTRO`).

---

## 4. Graphics, Audio, DMA/Timers, Cartridge

### 4.1 PPU (scanline renderer)

- Line timing: 1232 cycles (960 HDraw + 272 HBlank); 228 lines (160 visible + 68 VBlank).
- Each visible line is rendered at the HDraw-end event. Mid-scanline register changes are not modeled.
- **Layers** rendered into per-layer line buffers carrying color + priority + flags:
  - Text BGs (modes 0/1): 4bpp/8bpp tiles, H/V flip, 256×256 to 512×512 maps.
  - Affine BGs (modes 1/2): wraparound flag; internal reference points (BGxX/BGxY) latched at VBlank, incremented by PB/PD each line, reloaded when the game writes the registers.
  - Bitmap modes 3/4/5 with frame select.
  - OBJ: 128 sprites, regular + affine + double-size, 4bpp/8bpp, 1D/2D mapping, semi-transparent mode, OBJ-window mode, per-line OBJ cycle budget (1210 cycles, or 954 when `DISPCNT` H-Blank Interval Free is set).
  - Mosaic for BGs and OBJ.
- **Compositor:** per pixel, selects top two layers by priority; applies window masks (WIN0, WIN1, OBJWIN, outside); applies `BLDCNT` effects: alpha (`BLDALPHA` EVA/EVB), brighten/darken (`BLDY`); semi-transparent OBJ forces alpha blend with the second target.
- **NEON asm:** tile-row decode (4bpp/8bpp) and compositor/blend kernel, 8 pixels per iteration.
- **Output:** 240×160 BGR555 (`uint16_t`), converted on the GPU.

### 4.2 APU

- **PSG:** ch1 (square + sweep), ch2 (square), ch3 (wave; 2 banks × 32 4-bit samples, bank select/dimension), ch4 (noise; 7/15-bit LFSR). 512 Hz frame sequencer drives length, envelope, sweep.
- **DMA sound A/B:** 32-byte FIFOs of signed 8-bit samples; each clocked by timer 0 or 1 overflow; when a FIFO has ≤16 bytes it requests DMA1/DMA2 (special timing). `SOUNDCNT_H` volume (50/100%), L/R enables, FIFO reset.
- **Mixer:** `SOUNDCNT_L` PSG master volume and L/R routing, `SOUNDCNT_H` PSG ratio (25/50/100%), `SOUNDBIAS` bias; output clamped to 10-bit then scaled to int16.
- **Output rate:** fixed **32,768 Hz** stereo int16 (the hardware default `SOUNDBIAS` resolution). Output buffer holds ≥2 frames of samples; `gba_audio_read` drains it.

### 4.3 DMA

- 4 channels; priority DMA0 > DMA1 > DMA2 > DMA3.
- Start timing: immediate, VBlank, HBlank, special (DMA1/2: sound FIFO; DMA3: video capture — **stubbed**, not implemented in v1).
- Address control: increment, decrement, fixed, increment+reload (dest only).
- Word count 0 = 0x4000 (DMA0–2) or 0x10000 (DMA3). Repeat mode. IRQ on completion.
- Restrictions: DMA0 cannot read cartridge ROM; only DMA3 can access EEPROM.
- CPU stalled during transfer; cost `2N + 2(n−1)S + xI`.
- DMA open-bus latch retains last transferred value.

### 4.4 Timers, IRQ, keypad, SIO

- **Timers:** 4 × 16-bit, prescaler 1/64/256/1024, count-up cascade, reload on overflow, overflow IRQ; timers 0/1 feed sound FIFOs. Lazy evaluation per §3.1.
- **IRQ:** 14 sources (VBlank, HBlank, VCount, Timer0–3, Serial, DMA0–3, Keypad, GamePak). `IE`/`IF` (write-1-to-clear)/`IME`. Small IRQ-entry delay modeled.
- **Keypad:** `KEYINPUT`, `KEYCNT` interrupt in AND/OR modes.
- **SIO:** registers present; behaves as "no cable connected" (transfers complete with idle values, SIO IRQ raised if enabled) so games do not hang.

### 4.5 Cartridge

- **ROM:** up to 32 MiB; header parsed (title, game code, checksum — checksum failure is a warning only).
- **Save type detection:** scan ROM for library ID strings:
  - `SRAM_V`, `SRAM_F_V` → 32 KiB SRAM
  - `FLASH_V`, `FLASH512_V` → 64 KiB Flash (Panasonic device ID)
  - `FLASH1M_V` → 128 KiB Flash (Sanyo device ID, bank switching)
  - `EEPROM_V` → EEPROM; size (512 B vs 8 KiB) inferred from the first EEPROM DMA request length (9-bit vs 17-bit)
  - None found → no save
- **Game database** (built-in table keyed by game code + CRC32): overrides save type; enables peripherals.
- **GPIO port** (0x080000C4–0x080000C9):
  - RTC (Seiko S-3511 serial protocol) — game A, game E; fed by `gba_set_rtc`.
  - Solar sensor — game E 1–3; fed by `gba_set_light`.
  - Gyro — game D; fed by `gba_set_gyro`.
  - Rumble — game H; exposed via `gba_rumble_active`.
- **Tilt sensor** — game F, game G; mapped at 0x0E008xxx in the save space; fed by `gba_set_tilt`.
- **Save RAM** dirty flag exposed via API; frontend flushes ~1 s after the last write and on pause/background.

---

## 5. Android Platform Layer (`platform/android/`, C)

### 5.1 Threads

| Thread | Responsibilities |
|---|---|
| Emulation (pthread) | Read atomic key mask + sensor snapshot → `gba_run_frame` → (cheats applied inside core at VBlank) → publish frame into triple buffer → `gba_audio_read` → resample → push to audio ring. Paced by audio-ring fill level (condvar signalled by the audio callback). Owns the `ALooper` used for sensors. |
| Render (pthread) | EGL + GLES 3.0 context on the `SurfaceView`'s `ANativeWindow`; driven by `AChoreographer` frame callbacks. |
| Audio (AAudio data callback) | Copies from the SPSC ring into AAudio's buffer (silence when short); never blocks — the emulation thread paces itself by polling the ring's fill (milestone 4b). |

### 5.2 Video

- Triple buffer: three 240×160 BGR555 buffers; producer and consumer swap indices atomically; consumer always takes the newest completed frame.
- On each vsync: upload the newest frame with `glTexSubImage2D` into a `GL_R16UI` texture; draw a single quad; fragment shader unpacks BGR555 to RGB.
- If no new frame arrived, re-present the previous one (correct behavior on 90/120 Hz panels).
- Shader options: nearest, sharp-bilinear, LCD grid, GBA color correction.
- Scaling: aspect-correct fit or integer scale; screen rectangle comes from the active skin layout.

### 5.3 Audio

- Device native sample rate and frames-per-burst obtained once from `AudioManager` (Kotlin) and passed to native init.
- AAudio output stream (shared, low latency, stereo int16, the device's native rate read back from the stream — no Kotlin `AudioManager` query needed); details in `2026-09-26-milestone-4b-sound-design.md`.
- Resampler: 32,768 Hz → device rate, cubic (Hermite) interpolation; NEON kernel + C twin.
- Dynamic rate control: `ratio = base_ratio × (1 + 0.005 × (fill − 0.5) / 0.5)`, where `fill` is ring occupancy in [0, 1]. Target ring size ≈ 60 ms.
- Underrun → output silence for that buffer.
- **Fast-forward:** pacing disabled; audio discarded; frames-per-vsync capped by the selected speed (2×/4×/8×/unlimited); only newest frame presented.

### 5.4 Input & sensors

- **Touch overlay:** Kotlin custom `View` layered over the `SurfaceView`; multi-touch; diagonal D-pad zones; redraws only on pressed-state change.
- **Gamepads:** Kotlin `dispatchKeyEvent` / `onGenericMotionEvent` → remappable bindings → key mask.
- **Key mask transport:** `NativeBridge.setKeys(int)` on change → atomic store → read by emulation thread at frame start.
- **Sensors (C, `ASensorManager`):** enabled only when the game DB flags the peripheral.
  - Accelerometer → `gba_set_tilt`
  - Gyroscope → `gba_set_gyro`
  - Ambient light (lux) → mapped to `gba_set_light`; manual slider override in the in-game menu.
- **Rumble:** emulation thread detects state change of `gba_rumble_active` → JNI callback to Kotlin → `Vibrator` / `VibratorManager` (API 31+) and gamepad vibrator.
- **RTC:** emulation thread sets `gba_set_rtc` from `clock_gettime` + local UTC offset once per second.

### 5.5 Lifecycle

- `onPause`: pause emulation thread, flush save RAM, pause the AAudio stream, write auto-save state.
- `surfaceDestroyed`: destroy EGL surface, keep context; `surfaceCreated`: recreate surface, re-create textures/shaders if context was lost.
- `onResume`: reverse of pause.
- Library offers "continue" from the auto-save state.

### 5.6 JNI surface (`NativeBridge.kt`, ~20 functions)

`init(sampleRate, framesPerBurst)`, `loadRom(ByteBuffer)`, `loadBios(ByteBuffer)`, `setSurface(Surface?)`, `start()`, `pause()`, `resume()`, `stop()`, `setKeys(int)`, `setFastForward(speed)`, `setRewinding(bool)`, `saveState(path)`, `loadState(path)`, `getSaveRam()/putSaveRam()`, `addCheat(text)`, `enableCheat(id, bool)`, `clearCheats()`, `setOption(key, value)`, `setLightOverride(level)`, plus the Kotlin-side `onRumble(bool)` callback.

---

## 6. Save States, Rewind, Cheats

### 6.1 Save states

- **Format:** header `{ magic "GBAS", format_version u32, rom_crc32 u32, unix_time i64 }` followed by TLV chunks `{ tag u32, chunk_version u32, length u32, payload }`.
- **Chunks:** CPU, EWRAM, IWRAM, VRAM, OAM, PAL, IO, PPU (internal affine refs, etc.), APU, DMA, TMR, SCHED (event IDs + relative timestamps), CART (save RAM, GPIO/RTC/sensor state), THUMB (raw 240×160 BGR555).
- Decoded-block cache is not serialized; it is flushed on load.
- Per-chunk versioning allows loading older states after format changes.
- **Loading is atomic:** all chunks are parsed and validated into a scratch area before any core state is modified.
- **On disk:** whole state LZ4-compressed. Paths: `files/states/<crc32>/slot{0..9}.state` and `auto.state`.

### 6.2 Rewind

- Every `interval_frames` (default 4), serialize state (without THUMB) and store `LZ4(current XOR previous)` in a ring; keep one uncompressed "current" state.
- Step back = decompress newest delta, XOR into current, pop. No keyframes required. Eviction = drop the oldest delta.
- Default budget 64 MiB (configurable) ≈ 3+ minutes.
- While rewinding: one snapshot popped per presented frame, run 1 frame for display, audio muted. On release, emulation resumes from that point.
- XOR kernel in NEON asm + C twin.

### 6.3 Cheats

- **Formats:** raw (`AAAAAAAA:VV`, `:VVVV`, `:VVVVVVVV`), GameShark v1/v2 (TEA-based decryption with the known seeds), Action Replay MAX v3 (its seeds + opcode table), CodeBreaker (including encrypted variant).
- All decoded into an internal op list: `write8/16/32`, conditionals (`if ==, !=, <, >, &`) guarding the next op(s), ROM patch (applied as a ROM overlay with block-cache invalidation of the page).
- Master/enable codes accepted and ignored.
- Applied once per frame at VBlank start.
- Storage: per-game JSON in Kotlin; import of libretro `.cht` files.

---

## 7. Kotlin App Shell (`app/`)

- **UI toolkit:** Jetpack Compose for library, settings, menus. Game screen = `SurfaceView` + overlay `View` hosted via `AndroidView`.
- **ROM access:** Storage Access Framework `ACTION_OPEN_DOCUMENT_TREE` with persisted permission; scan for `.gba` and `.zip` (zip extracted with `java.util.zip`); ROM bytes passed to native as a direct `ByteBuffer`.
- **Library:** grid of games; title from ROM header; tile image = latest auto-save thumbnail; index persisted as a JSON file.
- **In-game menu:** save/load slot picker with thumbnails, fast-forward toggle, rewind, cheats, per-game settings, light-sensor slider (when applicable), quit.
- **Settings:** video filter, scaling mode, audio enable + latency, fast-forward speed, rewind enable + budget, controller bindings, overlay opacity + haptics, BIOS file, per-game overrides (save type, RTC enable).
- **Skins:** layout JSON (portrait + landscape; normalized coordinates for screen rect and each button) + PNG assets; drag/resize layout editor; import skin `.zip`. (6c, user decision 2026-10-02: the layout editor only — PNG button pictures and skin `.zip` import are not in v1; they are milestone 9, added 2026-10-04 — brainstormed as importing Delta emulator skins (`.deltaskin`), no own format.)
- **Saves:** `.sav` in `files/saves/<crc32>.sav`; export/import via SAF.

---

## 8. Error Handling

| Area | Behavior |
|---|---|
| Guest behavior | Never crashes the core. Undefined opcodes → Undefined exception (as hardware). Unmapped reads → open bus. Unknown I/O writes ignored. |
| ROM load | Size 0 or > 32 MiB → `GBA_ERR_ROM_SIZE`; UI shows error. Header checksum mismatch → warning only. |
| BIOS load | Size ≠ 16 KiB → `GBA_ERR_BIOS_SIZE`; fall back to HLE and notify. |
| Save state load | Magic/version/CRC checked; corrupt chunk → `GBA_ERR_STATE_CORRUPT`; core unchanged (atomic load). Toast in UI. |
| Save RAM write | Atomic (write temp + rename); previous file kept as `.sav.bak`. |
| Cheat parse | Unrecognized format → `GBA_ERR_CHEAT_FORMAT`; UI highlights the offending line. |
| GL | Surface recreation rebuilds textures/shaders; EGL init failure → error screen. |
| Audio | AAudio open failure (or a stuck stream the watchdog cannot revive) → run muted; emulation paced by `clock_nanosleep` at 59.7275 Hz. Device disconnect → reopen the stream from the emulation thread. |
| Debug logging | Core writes to an in-memory ring log in debug builds only; platform layer forwards to logcat. |

---

## 9. Testing Strategy

### 9.1 Host toolchain

- LLVM clang for Windows (to be installed) + CMake 3.31 and Ninja from `%LOCALAPPDATA%\Android\Sdk\cmake\3.31.6`.
- Debug host builds use `-fsanitize=address,undefined`.

### 9.2 Unit tests (`tests/unit/`, tiny in-house C harness)

- CPU: flag results for ALU ops and shifter edge cases (shift by 0/32/>32, RRX).
- Scheduler ordering; lazy timer reads across prescalers and cascades.
- DMA cycle counts and address control.
- HLE SWIs: Div, Sqrt, ArcTan2, LZ77, Huffman, RLE results against known vectors.
- Cheat decoders: known encrypted → decrypted pairs for GSv1/v2, ARv3, CodeBreaker.
- Save-state round trip: save → load → save yields identical bytes.
- Rewind XOR/LZ4 round trip.

### 9.3 Test ROMs (`tests/roms/`, gitignored; fetched by `tools/fetch_test_roms.py`)

- jsmolka gba-tests (arm, thumb, memory, bios, ppu, save), armwrestler, mGBA test suite (timing, DMA, I/O, video), tonc demos.
- `testrunner` runs N frames, hashes framebuffer and audio, compares with `tests/expected.json`; `--dump-png` / `--dump-wav` for debugging.

### 9.4 Replay regression

- Replay file = initial state + per-frame key masks + expected final hash. Recorded from user-owned games; stored in `tests/replays/` (states only, no ROM data).

### 9.5 Asm ↔ C equivalence

- Same replays run on an arm64 device with and without `GBA_FORCE_GENERIC`; per-frame framebuffer + audio hashes must match exactly. **Requires a physical arm64 device** (host emulator image is x86_64).

### 9.6 Performance

- Host benchmark mode: uncapped frames/sec.
- Target: ≥ 4× real time uncapped on a Cortex-A73-class core. Debug builds show an on-device frame-time overlay.

---

## 10. Milestones

| # | Milestone | Exit criteria |
|---|---|---|
| 1 | CPU, memory bus, HLE BIOS, scheduler, host testrunner | jsmolka arm/thumb/memory/bios pass |
| 2 | PPU, DMA, timers, PC viewer (spec: `2026-09-24-milestone-2-ppu-dma-timers-design.md`) | jsmolka ppu + tonc demos match approved frame fingerprints; viewer runs demos live |
| 3 | Split into 3a sound (spec: `2026-09-25-milestone-3a-sound-design.md`), 3b save chips (spec: `2026-09-25-milestone-3b-saves-design.md`), 3b-2 cartridge extras (spec: `2026-09-25-milestone-3b2-cart-extras-design.md`), 3c split into 3c-1 right values (spec: `2026-09-25-milestone-3c1-values-design.md`) and 3c-2 right timing (spec: `2026-09-25-milestone-3c2-timing-design.md`) | 3a: own sound ROMs + Tonc snd1_demo match approved audio fingerprints ✅; 3b: jsmolka save ROMs + own EEPROM ROMs pass, saves persist in gbaview; 3b-2: extras verified by unit tests replaying game routines + game A runs without "battery run dry" ✅; 3c-1 ✅: Memory, I/O read, Shifter, Carry, Multiply long, BIOS math, SIO register R/W at 100%; 3c-2 ✅ (finish line revised to mGBA level, 2026-09-25): Timing ≥ 1708/2020, DMA 1244/1244, Timer IRQ ≥ 70, Timer count-up / Misc / SIO timing as high as a time-boxed tuning task gets them (aim count-up ≥ 610) — reached Timing 1796, DMA 1244, Timer IRQ 71, count-up 690, Misc 6/12, SIO timing 4/4. **Milestone 3 ✅** |
| 4 | Android platform layer (GL, AAudio, threads, basic overlay). Split into 4a app + picture (spec: `2026-09-26-milestone-4a-app-picture-design.md`), 4b sound (spec: `2026-09-26-milestone-4b-sound-design.md`), 4c controls + save files + lifecycle (spec: `2026-09-26-milestone-4c-controls-saves-design.md`), 4d rumble + sensors (split off 4c by the user, 2026-09-26; spec: `2026-09-26-milestone-4d-rumble-sensors-design.md` — sensors are read in Kotlin, not with `ASensorManager` in C as §5.4 sketched) | Games playable on x86_64 emulator. 4a ✅: the user opens a ROM with the file picker on the Pixel 9 emulator; full-screen, right speed and colours, PC-keyboard play; the Android-built core reproduces all 32 PC fingerprints (tag `milestone-4a`). 4b ✅: games play with sound on the Pixel 9 emulator (AAudio, cubic rate conversion, the sound paces the game; plus a key latch so taps shorter than a frame reach the game), the user's listening check passed (tag `milestone-4b`). 4c ✅: on-screen buttons (layout A), save files in the app (written ~1 s after the game saves and on pause/quit, `.sav.bak`), `.sav` import, Continue, Back question, one game screen (`singleTask`); the user played game A touch-only with their imported PC save (tag `milestone-4c`). 4d ✅: tilt / turn / light from the phone's sensors (read in Kotlin) + I/J/K/L, Q/E keys + a ☀ light slider, rumble on the phone's vibrator, `dev.sh phone-install`; the user played game D on the emulator (Q/E) and on their phone (turning, rumble) (tag `milestone-4d`). **Milestone 4 complete.** |
| 5 | Save states, rewind, fast-forward, cheats. Split (user decision 2026-09-27) into 5a save states + in-game menu (spec: `2026-09-27-milestone-5a-save-states-design.md`), 5b fast-forward + rewind (spec: `2026-09-27-milestone-5b-fast-forward-rewind-design.md`), 5c cheats (spec: `2026-09-28-milestone-5c-cheats-design.md` — Action Replay v3 / GameShark v1-v2 / CodeBreaker / address:value; `.cht` import left for later) | Round-trip + replay tests pass. 5a ✅: 10 picture slots per game, an automatic state resumed on open, Back opens the in-game menu (Resume / Save state… / Load state… / Restart game / Quit); all 32 fingerprints replay exactly from a state loaded into a new core on the PC and the emulator (CTest 88); the user saved/loaded states in game A, Continue resumed the exact moment, Restart game worked (tag `milestone-5a`). 5b ✅: fast-forward by tap or hold (⏩, gamepad R2, F as a tap switch on the laptop) at 2×/4×/8×/Max from the in-game menu, the sound sped up (Max silent); rewind by holding ⟲ / L2 / R, ~2 min 40 s in game A (64 MiB, a packed snapshot every 4 frames — our own packer, not LZ4); all 32 fingerprints replay exactly through a rewind on the PC and the emulator (CTest 120); the user's check passed (tag `milestone-5b`). 5c ✅: Action Replay v3 / GameShark v1-v2 (TEA-scrambled or plain), CodeBreaker (plain or scrambled) and address:value codes, auto-detected with a type picker, run at the end of every frame by the core (ROM patches undone when switched off; never in a save state or rewind); a cheat panel in the in-game menu with an add/edit pop-up; each game's cheats in `files/cheats/<game id>.json`, switched back on when it opens; the user added real Action Replay and CodeBreaker codes in game A and they worked (tag `milestone-5c`). **Milestone 5 complete.** |
| 6 | App shell: library, settings, skins + editor, gamepads, sensors, rumble. Split (user decision 2026-09-28) into 6a game library (spec: `2026-09-28-milestone-6a-library-design.md`), 6b settings + controller remapping (spec: `2026-09-28-milestone-6b-settings-design.md`), 6c the touch layout editor (spec: `2026-10-02-milestone-6c-layout-editor-design.md`; button pictures and skin `.zip` import left out of v1 by the user, 2026-10-02) (sensors and rumble were done in 4d) | Feature scope in §7 complete. 6a ✅: a games folder picked once (a folder inside Download — Android refuses Download itself), `.gba` and `.zip` games listed with the picture of where each was left and when it was last played, games opened from elsewhere kept in the list, hold for Start fresh / Import save / Export save / Remove from list; the user's check passed (tag `milestone-6a`). 6b ✅: a Settings screen (from the library and the in-game menu) — picture sharp/smooth and fit / whole-number / stretch, sound on/off and delay, fast-forward speed, rewind on/off and length, keyboard + controller remapping, the touch buttons' sideways look and buzz, a BIOS file (a state of the other BIOS kind refused by the core); per-game save type and clock; the user's check passed (tag `milestone-6b`). 6c ✅: a touch layout editor (Settings → Touch buttons → Edit upright / sideways layout…) — every button and the game picture dragged anywhere and sized ½×–2× with a slider, upright and sideways separately, buttons over the picture see-through, Reset all / Cancel / Save, kept in `files/layout.json`; the user's check passed (tag `milestone-6c`). **Milestone 6 ✅.** |
| 7 | ARM64 asm paths. Split (user decision 2026-10-02) into 7a measuring + C speed-ups (spec: `2026-10-02-milestone-7a-speed-design.md`; goal: run well on cheap and old phones, measured on the user's phone's small core) and 7b ARM64 assembly where 7a's measurements show it still pays | Equivalence tests pass on device; performance target met (7a: the finish-line speed set with the user after the prototype's measurements, aiming at ≥ 4× real time on the small core). 7a ✅: `gbabench` (6 speed scenes, PC and phone, a profiler), C speed-ups that change no result (text BGs by tile row, the compositor looking at each layer once, bitmap rows, instruction fetch and RAM/ROM data straight from memory, an ARM condition table, link-time optimisation on Android), Settings → Picture → Show speed; on phone B's small core game A 1.23 → 2.04×, game B 0.84 → 1.22×, game C 1.60 → 3.24×, game D 1.13 → 1.63×; every fingerprint unchanged on the PC, the emulator and the phone; the user's check passed (tag `milestone-7a`). 7b ✅ (spec: `2026-10-03-milestone-7b-asm-design.md`): the CPU loop in ARM64 assembly (`core/arch/arm64/cpu_a64.S`) with a threaded C loop as its plain-C twin (`cpu_run_generic`, also on the PC and the x86_64 emulator), NEON for the compositor and 4bpp tile rows; remembered instructions (slower) and NEON sound (under 5 % of a frame) tried and left out; `GBA_FORCE_GENERIC` / `GENERIC=1` builds the arm64 core with only the twins; twin tests on random ARM/Thumb programs on the phone; on phone B's small core game A 2.04 → 4.65×, game B 1.22 → 3.52×, game C 3.24 → 6.41×, game D 1.63 → 3.37× (the agreed finish line: 95 % of the prototype); every fingerprint unchanged on the PC, the emulator and phone B both builds; the user's check passed (tag `milestone-7b`). **Milestone 7 ✅.** |
| 8 | App polish + skins (added by the user 2026-10-03). Split into 8a the new look (spec: `2026-10-03-milestone-8a-app-look-design.md`: own colours, clearer menus, icons, no "…") and 8b console skins (touch buttons in the colours of special-edition handhelds; spec after 8a) | The user finds the screens easier and nicer and the skins look right; everything works as before; all tests pass. 8a ✅: the 'Cartridge grey' theme (`AppTheme.kt`), the game list as a grid with a Continue card and a ⋮ sheet on every card, a pause menu with Resume, four tiles, the speed as choice buttons and Restart / Quit (two columns sideways), Settings with switches and choice buttons and help under each setting, Material icons, no "…" / "&" in any label (a test reads every string literal); Kotlin 88, on-device 56, the core unchanged; the user's check passed (tag `milestone-8a`). 8b ✅ (spec `2026-10-04-milestone-8b-console-skins-design.md`): 13 skins (Plain, the default, as before; Ice, Pink, Lime, White, Black, Orange, Yellow, Gold, Silver, Red and cream, Grey and red, Classic grey) colouring the body round the game picture (the game screen's background and OpenGL's clear colour, `NativeBridge.setBackground`) and the touch buttons (each skin's colours, darker when pressed, a rim, a lit fast-forward, labels ≥ 4.5:1), see-through only over the game itself (`TouchLayout.game`); one skin for all games, picked on Settings → Touch buttons → Skin (a page of small phone previews); Kotlin 96, on-device 58, the core unchanged; Claude's check on the emulator passed (no user check, user decision) (tag `milestone-8b`). **Milestone 8 ✅.** |
| 9 | Button pictures + skin import (added by the user 2026-10-04; dropped from 6c's v1) → **Delta skins** (spec `2026-10-04-milestone-9-delta-skins-design.md`): the user imports `.deltaskin` files made for the Delta emulator — one PDF or PNG picture of the whole controller with touch areas; no own format, no per-button pictures | ✅ Skins imported from the phone's files into `files/skins/<n>/` (checked: GBA only, a phone layout, pictures present, ≤ 50 MB / 500 files, safe paths), picked and removed on the Skin page; the picture drawn sharp at the phone's size (PDFs by `PdfRenderer`, kept), the game in the skin's screen, pressed buttons darkened, Delta's Menu / fast-forward / quick save and load (a new Quick slot) working; the layout editor off for the directions a skin covers; Kotlin 119, on-device 63, the core unchanged; Claude's check on the emulator and the user's try passed (tag `milestone-9`). **Milestone 9 ✅.** |
| 10 | Cheat-file import, cloud sync, box art (added by the user 2026-10-04): import libretro `.cht` cheat files (left for later in 5c), sync saves / states across devices, box art in the game grid (the first network features — v1 had none). Own spec(s) after 9. Split (user decision 2026-10-04) into **10a cheat files** (spec `2026-10-04-milestone-10a-cheat-files-design.md`: pick a `.cht`, all its cheats added switched off, Remove all; offline) → **10b box art** (the first internet use, download only) → **10c cloud sync** | Done — **10a ✅** (tag `milestone-10a`: `.cht` import into a game's cheats, all added switched off, unreadable ones and duplicates skipped with a note, Remove all; Kotlin 128, on-device 64, the core unchanged; Claude's check with libretro's game A file passed); **10b ✅** (tag `milestone-10b`: box art on the grid's small cards from libretro, found by fingerprint or game code, downloaded in the background (a Settings switch), Choose / Remove box art, Find cheats online; the app's first internet use; Kotlin 152, on-device 70, the core unchanged; Claude's check passed); **10c cloud sync ✗ cancelled** by the user 2026-10-04 after its spec and plan, before any code (local saves only — users back up their saves themselves, without the app's help; fewer integrations and app keys helps licensing and third-party audits when the app goes public; spec `2026-10-04-milestone-10c-cloud-sync-design.md` kept as a record) — milestone 10 complete |
| 11 | Backup and restore (added by the user 2026-10-04, after cancelling 10c; spec `2026-10-04-milestone-11-backup-design.md`): one `.zip` with saves, states, settings and controls, cheats, imported skins and chosen box art, made by hand ("save as") or automatically into a picked folder; restore replaces everything with a safety copy and Undo; no BIOS, game list or ROMs in it; Android's own backup off | All tests pass and Claude's emulator check passes (no user check) — **done 2026-10-04** (tag `milestone-11`) |
| 12 | The UI overhaul — **Game Pak** (added by the user 2026-10-05; spec `2026-10-05-milestone-12-game-pak-ui-design.md`, shaped with the impeccable skill, product record `PRODUCT.md`): every screen rebuilt as a cartridge collection — moulded-plastic screens (light grey by day, smoked black by night, following the phone), games as cartridges with their picture as the label, the last-played game seated in the slot, an accent that follows the console skin, the phone's own fonts; a new app icon and launch screen. Replaces 8a's look; the core and the touch buttons over the game are untouched | Done (tag `milestone-12`): the finish review `ship`, `DESIGN.md` written, tests Kotlin 186 / on-device 78 / CTest 120, the user's check passed ("It all works"). Done when the user's check, Claude's emulator check (light/dark, upright/sideways, font 1.3×), the impeccable finish review (`ship`) and all tests pass, and `DESIGN.md` is written |
| 13 | The game check (added by the user 2026-10-05, as "finish emulation features"; spec `2026-10-05-milestone-13-game-check-design.md`): the user's games plus ~20–30 free homebrew games, each run with a button script; contact-sheet pictures and a warning log of anything unsupported (a PC-only `build/check` build); real problems fixed, big fixes (e.g. mid-line drawing) only with the user's OK Every game runs its script with no crash or freeze and looks right to Claude; every warning fixed or explained; every game locked in (`dev.sh games --check`); old tests, mGBA suite scores, PC speed (≥ 97 %) and old save states unharmed; no user check — **met 2026-10-05** (tag `milestone-13`) |
| 14 | RetroAchievements (added by the user 2026-10-05; own spec after 13): the user's own retroachievements.org sign-in through the rcheevos library — the one exception to the no-sign-ins rule (no app key needed) | Own spec: `2026-10-06-milestone-14-retroachievements-design.md` — **met 2026-10-06** (tag `milestone-14`: all tests, Claude's emulator check, impeccable `ship`, and a real unlock on the user's account: "Hey, The Game Works!" in game D, softcore) |
| 15 | RetroAchievements leaderboards (added by the user 2026-10-06; spec `2026-10-06-milestone-15-leaderboards-design.md`): a Leaderboards page in the pause menu (top 10 + around the user) and the live parts (notes, counter pills, the submitted score with its rank) — rcheevos runs boards only in hardcore, so real scores wait for RetroAchievements' approval of the app | Done (tag `milestone-15`): the Leaderboards page (top 10 + around the user, your rank on a printed sticker, opens on your row), counters, notes and the rank card over the game in hardcore, the "Leaderboard notes on screen" switch; impeccable `ship`; the user saw game D's real scores; unit 474, CTest 121, Kotlin 236, on-device 88, `games --check` 31/31 |
| 16 | Release prep — DreamBoy 1.0 on GitHub (added 2026-10-26; spec `2026-11-12-milestone-16-release-prep-design.md`): the new name and app id, version 1.0.0, About + licences + privacy policy, offline unlocks kept, a play-first README, a signed release, published on GitHub | Done 2026-11-12: public at `https://github.com/tengencode/DreamBoy`, release v1.0.0 (`DreamBoy-1.0.0.apk`, signed with the user's key); history scrubbed of personal details before going public; unit 485, CTest 123, Kotlin 269, on-device 93, `games --check` 31/31 |
| 17 | Game Boy and Game Boy Color (added by the user 2026-11-12; spec `2026-11-12-milestone-17-game-boy-design.md`): our own second core (`core_gb/`) behind a shared layer, `.gb` / `.gbc` games in the same list; 17a the engine on the PC → 17b the app + release 1.1.0 → 17c the extras (cheats, RetroAchievements, Delta skins, colour choices, rumble and tilt) | In progress — see the milestone's spec §9 |

Each milestone gets its own implementation plan. ARM64 assembly is added last so every asm routine replaces an already-verified C implementation.

---

## 11. Legal

- No BIOS, ROMs, or copyrighted test material shipped or committed.
- HLE BIOS written from scratch from public hardware documentation (GBATEK).
- Third-party code limited to permissively licensed libraries: today only rcheevos 12.5.0 (MIT, milestone 14; listed in `third_party/README.md`). LZ4 (BSD-2) was planned for rewind but never used — 5b wrote its own packer.
- Release (user decision 2026-10-23): **free and open source** — no paid version, ads or in-app purchases; licence **GPL-3.0-or-later** (`LICENSE`, user decision 2026-10-24); published on GitHub only as **DreamBoy** (milestone 16: public at `https://github.com/tengencode/DreamBoy` since 2026-11-12, release v1.0.0; no Play Store). No commercial game titles or personal details in the repository (2026-10-24). The public name and icon must not use Nintendo's marks; store screenshots must not show copyrighted games. Release-prep findings (RetroAchievements' approval rules, Google Play, Android developer verification) are in `docs/PROGRESS.md` Notes and the milestone 16 spec; how to make a release: `docs/RELEASE.md`.
