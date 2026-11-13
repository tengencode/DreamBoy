# Milestone 17a — The Game Boy / Game Boy Color Core on the PC — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task
> (inline in one session — `CLAUDE.md`; never subagent-driven). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A second emulator core, `gbcore`, that plays Game Boy and Game Boy Color games on the PC, proven by public
test ROMs, behind a small "machine" layer the PC tools use for both consoles — with nothing changed for GBA.

**Architecture:** `core_gb/` is a plain-C library beside `core/` with a `gb_*` API shaped like `gba_*`. The CPU steps
by machine cycle (every bus access first advances timer, screen, sound and serial by 4 ticks); the screen is a line
renderer; no event scheduler. `machine/` forwards one set of calls to `gba_*` or `gb_*`; rewind's ring moves to
`core_common/` so both cores share it.

**Tech Stack:** C11 (clang, llvm-mingw on the PC), CMake + Ninja via `bash tools/dev.sh`, the in-house unit harness
(`tests/unit/test.h`), CTest, Python 3 for the fetch / check scripts.

**Spec:** `docs/superpowers/specs/2026-11-12-milestone-17-game-boy-design.md` (§5, §8, §9.1, §10). Read it with this.

## Global Constraints

- Test-first; one commit per task; commit messages end with `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`.
- Invoke the `ponytail` skill before writing code; `ponytail-review` on each finished task's diff.
- After every task: tick its boxes here, update `docs/PROGRESS.md` (Completed, status table, Next), check no document
  is stale; reply in layman mode.
- Public-safe: no commercial game titles or personal details in tracked files or commit messages; test data uses
  made-up names. `python tools/public_scan.py` says `clean` before each commit.
- No ROMs or boot ROMs committed (only our own hand-built test ROMs). Downloads only through
  `tools/fetch_test_roms.py`, pinned by SHA-256 (the user allowed the Game Boy test-ROM downloads 2026-11-12).
- The core is written from public hardware documentation (Pan Docs, the test ROMs' notes), not from other emulators.
- `core_gb/` depends only on libc (+ `core_common/`); no OS, threads or files; heap only at create / load / rewind
  setup; never crashes on anything a game does.
- Frame 160×144 BGR555; sound stereo int16 at 32,768 Hz; one frame = 70,224 ticks, always.
- **GBA must not change:** `bash tools/dev.sh test` keeps every existing test passing (unit 485, CTest 123 on
  2026-11-12), `bash tools/dev.sh games --check` 31/31, PC speed within the floors in PROGRESS.md Notes.
- Build and test only through `bash tools/dev.sh <configure|build|unit|test|run>`.
- Branch `milestone-17`.

## Review Focus

1. **A ROM file that lies about itself** (header size bytes bigger than the file, RAM size byte out of range, a
   32 KiB file claiming MBC5 with 8 MiB): loads without reading or writing outside its buffers — Task 1 and Task 4.
2. **A save file of the wrong length** (empty, 1 byte, 3× too long, footer cut in half): `gb_save_read` never reads or
   writes out of bounds and the game still starts — Task 9.
3. **A save state from a file someone else made** (right magic, wrong ROM; right ROM, a bank number or pointer-like
   index out of range; truncated): refused or made safe, machine unchanged on refusal — Task 10.
4. **The screen switched off for many frames, then on** (LCDC bit 7): `gb_run_frame` still returns after 70,224 ticks
   every time, sound keeps coming, no hang — Task 6.
5. **A game that never returns from STOP or HALT with interrupts off:** `gb_run_frame` still returns each frame —
   Task 2.

## File map

| Path | Holds |
|---|---|
| `core_common/` (`CMakeLists.txt`, `delta_pack.c/.h`, `rewind_ring.c/.h`) | The packed-difference ring, moved out of `core/src` (Task 10) |
| `core_gb/CMakeLists.txt` | target `gbcore` (static), `-Wall -Wextra -Wno-unused-parameter` |
| `core_gb/include/gb.h`, `gb_debug.h` | the public API, the test/debug API |
| `core_gb/src/gb_internal.h` | `struct gb` and every component struct |
| `core_gb/src/gb.c` | create / load / reset / run_frame / keys / info |
| `core_gb/src/cpu.c` | the SM83 interpreter, interrupts |
| `core_gb/src/bus.c` | memory map, I/O registers, `gb_tick` (advance the hardware 4 ticks) |
| `core_gb/src/timer.c`, `serial.c`, `joypad.c` | as named |
| `core_gb/src/cart.c`, `rtc.c` | header, MBC1/2/3/5, cartridge RAM, the clock, the save file |
| `core_gb/src/ppu.c`, `ppu_draw.c` | mode / STAT / LY timing; the line renderer (both consoles) |
| `core_gb/src/apu.c` | the four channels, sequencer, mixer, output buffer |
| `core_gb/src/cgb.c` | speed switch, RAM banks, palettes, HDMA |
| `core_gb/src/state.c` | save states, rewind glue |
| `machine/` (`CMakeLists.txt`, `machine.c`, `machine.h`) | the shared layer |
| `tests/unit/test_gb_*.c`, `gbtestutil.c/.h` | unit tests (the existing `unit_tests` executable links `gbcore`) |
| `tests/gb_tests.json` | the Game Boy test-ROM list for CTest (name, rom, mode, expectation) |

## The public API (`core_gb/include/gb.h`) — fixed here, built up task by task

```c
typedef struct gb gb_t;
typedef enum { GB_OK = 0, GB_ERR_ROM_SIZE, GB_ERR_UNSUPPORTED_CART, GB_ERR_NOMEM, GB_ERR_STATE } gb_err_t;
enum { GB_KEY_A = 1 << 0, GB_KEY_B = 1 << 1, GB_KEY_SELECT = 1 << 2, GB_KEY_START = 1 << 3,
       GB_KEY_RIGHT = 1 << 4, GB_KEY_LEFT = 1 << 5, GB_KEY_UP = 1 << 6, GB_KEY_DOWN = 1 << 7 }; /* = GBA_KEY_* */
#define GB_WIDTH 160
#define GB_HEIGHT 144
#define GB_AUDIO_RATE 32768
#define GB_FRAME_TICKS 70224

gb_t *gb_create(void);
void gb_destroy(gb_t *g);
gb_err_t gb_load_rom(gb_t *g, const uint8_t *rom, size_t size); /* copies; resets */
void gb_reset(gb_t *g);
int gb_is_color(const gb_t *g);                 /* 1: runs as a Game Boy Color (header 0x143 bit 7) */
void gb_run_frame(gb_t *g);                     /* exactly GB_FRAME_TICKS */
void gb_set_keys(gb_t *g, uint8_t pressed);
const uint16_t *gb_framebuffer(const gb_t *g);  /* 160x144 BGR555 */
size_t gb_audio_read(gb_t *g, int16_t *out, size_t max_frames);
const char *gb_rom_title(const gb_t *g);        /* header title, printable ASCII only, up to 16 chars */
uint32_t gb_rom_crc32(const gb_t *g);
const uint8_t *gb_wram(const gb_t *g);          /* 0x8000 bytes (8 banks of 0x1000) */

size_t gb_save_size(const gb_t *g);             /* 0: no battery save. RAM (+48 with a clock) */
size_t gb_save_write(const gb_t *g, uint8_t *buf, size_t cap);     /* bytes written; 0 if cap too small */
void gb_save_read(gb_t *g, const uint8_t *data, size_t size);      /* forgiving: spec §5.5 */
int gb_save_dirty(gb_t *g);                     /* 1 if the game wrote cartridge RAM since the last call */
void gb_set_rtc(gb_t *g, int64_t unix_seconds); /* "it is now this time" */

size_t gb_state_size(const gb_t *g);
size_t gb_state_save(const gb_t *g, void *buf, size_t cap);
gb_err_t gb_state_load(gb_t *g, const void *buf, size_t len);
gb_err_t gb_rewind_configure(gb_t *g, size_t budget_bytes, int interval_frames);
int gb_rewind_step_back(gb_t *g);
```

`gb_debug.h`: `uint16_t gb_debug_reg(const gb_t*, int which)` (`GB_REG_AF, BC, DE, HL, SP, PC`),
`uint8_t gb_debug_peek(const gb_t*, uint16_t addr)` (no side effects), `void gb_debug_step(gb_t*)` (one instruction),
`uint64_t gb_debug_ticks(const gb_t*)`, `size_t gb_debug_serial(const gb_t*, char *out, size_t cap)` (every byte the
game sent on the serial port so far, at most 4096 kept), `int gb_debug_breakpoint(const gb_t*)` (1 once the game ran
`LD B,B`, opcode 0x40 — the mooneye "test finished" mark).

---

### Task 1: The library skeleton and cartridge header

**Files:** create `core_gb/CMakeLists.txt`, `core_gb/include/gb.h`, `core_gb/include/gb_debug.h`,
`core_gb/src/gb_internal.h`, `core_gb/src/gb.c`, `core_gb/src/cart.c`, `tests/unit/gbtestutil.c/.h`,
`tests/unit/test_gb_cart_header.c`; modify root `CMakeLists.txt` (add_subdirectory(core_gb) next to `core`, also in
the `GBA_BENCH_ONLY` branch), `tests/unit/CMakeLists.txt` (link `gbcore`, include `core_gb/src`).

**Interfaces — produces:** `gb_create`, `gb_destroy`, `gb_load_rom`, `gb_is_color`, `gb_rom_title`, `gb_rom_crc32`;
test helper `uint8_t *gbtu_rom(size_t size, uint8_t cart_type, uint8_t ram_code, uint8_t cgb_flag)` (a zeroed ROM
with a valid header: size byte 0x148 from `size`, header checksum 0x14D right, title "TEST"; caller frees) and
`gb_t *gbtu_boot(const uint8_t *code, size_t n)` (a 32 KiB no-chip ROM with `code` at 0x150 and `JP 0x150` at 0x100,
loaded).

- [x] **Step 1: Failing tests** (`test_gb_cart_header.c`)
  - `gb_load_accepts_plain_32k`: `gbtu_rom(0x8000, 0x00, 0, 0)` → `GB_OK`, `gb_is_color == 0`, title `"TEST"`.
  - `gb_load_color_flag`: cgb_flag 0x80 and 0xC0 → `gb_is_color == 1`; 0x00 → 0.
  - `gb_load_rejects_sizes`: size 0, 0x100 (shorter than a header), 8 MiB + 1 → `GB_ERR_ROM_SIZE`.
  - `gb_load_rejects_unknown_chip`: cart types 0x20 (MBC6), 0x22 (MBC7), 0xFC (camera), 0xFF (HuC1), 0x0B (MMM01)
    → `GB_ERR_UNSUPPORTED_CART`; types 0x00, 0x01–0x03, 0x05, 0x06, 0x0F–0x13, 0x19–0x1E → `GB_OK`.
  - `gb_load_header_lies_about_size`: a 32 KiB file whose 0x148 says 8 MiB and type MBC5 → `GB_OK` (the file's real
    size wins; ROM banks wrap to the file), and a file of 48 KiB (not a power of two) → `GB_OK`, padded with 0xFF to
    64 KiB.
  - `gb_load_bad_ram_code`: RAM code 0x07 (undefined) → `GB_OK` with no RAM.
  - `gb_load_bad_checksum_ok`: wrong 0x14D → `GB_OK`.
  - `gb_crc_matches_zlib`: the CRC-32 of a known 32 KiB buffer equals a value computed in the test with a bitwise loop.
  - `gb_title_printable_only`: title bytes `'A', 0x01, 'B', 0x80` → `"AB"`.
- [x] **Step 2:** `bash tools/dev.sh unit gb_` → fails to build (no `gb.h`).
- [x] **Step 3: Implement.** RAM sizes by code: 0 → 0, 1 → 0 (unused code), 2 → 8 KiB, 3 → 32 KiB, 4 → 128 KiB,
  5 → 64 KiB; MBC2 always 512 bytes. ROM held as a power-of-two copy (minimum 32 KiB) with a bank mask.
- [x] **Step 4:** `bash tools/dev.sh unit gb_` → all pass; `bash tools/dev.sh unit` → the old tests still pass.
- [x] **Step 5:** Commit `gb: the core's skeleton and cartridge header`.

### Task 2: The CPU

**Files:** create `core_gb/src/cpu.c`, `core_gb/src/bus.c`, `core_gb/src/gb_debug.c`,
`tests/unit/test_gb_cpu.c`, `test_gb_cpu_alu.c`, `test_gb_cpu_irq.c`; modify `gb.c`, `gb_internal.h`.

**Interfaces — produces:** `gb_reset` (post-boot registers: DMG `AF=01B0 BC=0013 DE=00D8 HL=014D SP=FFFE PC=0100`;
CGB `AF=1180 BC=0000 DE=FF56 HL=000D`), `gb_run_frame`, `gb_debug_*` (all but `serial`); internal
`void gb_tick(gb_t *g)` (4 ticks: called by `bus_read` / `bus_write` / internal-delay cycles before the access; in
this task it only counts ticks), `uint8_t bus_read(gb_t*, uint16_t)`, `void bus_write(gb_t*, uint16_t, uint8_t)`
(with the tick), `bus_peek` (without). Memory in this task: ROM bank 0/1 flat, WRAM 0xC000–0xDFFF + echo, HRAM, IF
(0xFF0F, upper 3 bits read 1), IE (0xFFFF); everything else reads 0xFF.

- [x] **Step 1: Failing tests** — each runs a few opcodes with `gbtu_boot` + `gb_debug_step` and checks registers
  with `gb_debug_reg` and ticks with `gb_debug_ticks`:
  - `gb_cpu_ld_*`: 8-bit and 16-bit loads, `LD (nn),SP`, `LDH`, `PUSH/POP` (`POP AF` clears F's low nibble).
  - `gb_cpu_alu_flags`: table-driven — `ADD/ADC/SUB/SBC/AND/XOR/OR/CP`, `INC/DEC`, `ADD HL,rr`, `ADD SP,e`,
    `LD HL,SP+e`, `DAA` (after add and after subtract), `CPL/SCF/CCF`, with exact Z N H C for boundary values
    (0x0F+1, 0xFF+1, 0x80−1, carry-in cases).
  - `gb_cpu_rot_shift_bits`: `RLCA/RLA/RRCA/RRA` (Z always 0), all 256 CB opcodes against a C model in the test
    (one loop: for each opcode and three input values, compare result and flags).
  - `gb_cpu_jumps`: `JP/JR/CALL/RET/RST`, conditional taken and not taken — destination and tick counts
    (`JR` taken 12 / not 8; `CALL` 24 / 12; `RET cc` 20 / 8; `JP` 16 / 12).
  - `gb_cpu_ticks_table`: every unprefixed opcode's tick count (not taken where conditional) against a 256-entry
    table in the test; every CB opcode: 8, `(HL)` forms 16, `BIT n,(HL)` 12.
  - `gb_cpu_irq_dispatch`: IME on, IE & IF bit 0 → jumps to 0x40, 20 ticks, IF bit cleared, IME off; priority
    (lowest bit first); `EI` takes effect after the next instruction; `DI`; `RETI`.
  - `gb_cpu_halt`: HALT with IME off and a pending interrupt later → wakes without jumping; **halt bug** (IME off,
    interrupt already pending): the byte after HALT runs twice.
  - `gb_cpu_illegal_opcode_hangs_safely`: opcode 0xD3 → the CPU stops advancing PC, and `gb_run_frame` still
    returns with `gb_debug_ticks` advanced by 70,224.
  - `gb_run_frame_always_returns` (Review Focus 5): `DI; HALT` and `DI; STOP` programs — three `gb_run_frame` calls
    return, ticks advance 3 × 70,224.
- [x] **Step 2:** run → fail.
- [x] **Step 3: Implement** a `switch` interpreter over the opcode (ponytail: no table generator). `gb_run_frame`
  runs instructions until the frame's tick budget (70,224, carried over by the overshoot) is used.
- [x] **Step 4:** `bash tools/dev.sh unit gb_` → pass.
- [x] **Step 5:** Commit `gb: the SM83 CPU`.

### Task 3: Timer, serial port, joypad

**Files:** create `core_gb/src/timer.c`, `serial.c`, `joypad.c`, `tests/unit/test_gb_timer.c`, `test_gb_io.c`;
modify `bus.c`, `gb_debug.c`.

**Interfaces — produces:** `timer_tick(gb_t*)` etc. called from `gb_tick`; registers DIV 0xFF04, TIMA 05, TMA 06,
TAC 07 (upper 5 bits read 1), SB 01, SC 02, P1 00; `gb_set_keys`; `gb_debug_serial`.

- [x] **Step 1: Failing tests**
  - `gb_div_counts`: DIV = high byte of a 16-bit counter that advances with every tick; writing any value resets it.
  - `gb_tima_rates`: TAC 0b100/101/110/111 → TIMA +1 every 1024 / 16 / 64 / 256 ticks.
  - `gb_tima_overflow_delay`: on overflow TIMA reads 0 for 4 ticks, then TMA is loaded and IF bit 2 set; a write to
    TIMA during those 4 ticks cancels the reload and the interrupt.
  - `gb_tima_div_write_edge`: writing DIV while the selected counter bit is 1 ticks TIMA once; switching TAC from a
    rate whose bit is 1 to disabled ticks TIMA once.
  - `gb_serial_no_cable`: writing SC = 0x81 → after 8 × 512 ticks SB reads 0xFF, SC bit 7 clear, IF bit 3 set, and
    the byte written to SB appears in `gb_debug_serial`; SC = 0x80 (external clock) never finishes.
  - `gb_joypad_matrix`: P1 select lines (bit 4 = directions, bit 5 = buttons), pressed reads 0, bits 6–7 read 1;
    pressing a selected key sets IF bit 4.
- [x] **Step 2–4:** fail, implement (the timer as a falling-edge detector on `counter bit & enable`), pass.
- [x] **Step 5:** Commit `gb: timer, serial port, joypad`.

### Task 4: Cartridge chips

**Files:** modify `cart.c`, `bus.c`; create `tests/unit/test_gb_mbc.c`.

**Interfaces — produces:** `cart_read(gb_t*, uint16_t)`, `cart_write(gb_t*, uint16_t, uint8_t)` for 0x0000–0x7FFF
and 0xA000–0xBFFF; cartridge RAM buffer + dirty flag (`gb_save_dirty`).

- [x] **Step 1: Failing tests** — ROMs built with `gbtu_rom`, each bank's first byte = its bank number:
  - `gb_mbc1_banking`: bank 0 register value → bank 1; 5-bit low + 2-bit high; mode 1 moves the high bits to the
    0x0000 area and to RAM banking; RAM off reads 0xFF and ignores writes until 0x0A is written to 0x0000–0x1FFF;
    bank numbers wrap to the ROM's size.
  - `gb_mbc2_ram`: 512 half-bytes, upper nibble reads 1s, mirrored through 0xA000–0xBFFF; address bit 8 picks RAM
    enable vs ROM bank; bank 0 → 1.
  - `gb_mbc3_banking`: 7-bit ROM bank (0 → 1), RAM banks 0–3 (0–7 with a 64 KiB RAM: the MBC30 case), clock register
    selects 0x08–0x0C read 0 for now (Task 9).
  - `gb_mbc5_banking`: 9-bit ROM bank (bank 0 is really bank 0), 4-bit RAM bank, rumble bit 3 ignored on rumble types.
  - `gb_nochip_ram`: type 0x08/0x09 with 8 KiB RAM.
  - `gb_cart_no_ram_reads_ff`, `gb_cart_dirty_flag` (set by a RAM write, cleared by `gb_save_dirty`).
  - `gb_cart_lying_header_safe` (Review Focus 1): a 32 KiB MBC5 file claiming 8 MiB and 128 KiB RAM with RAM code
    0x04: select bank 0x1FF and RAM bank 0xF, read and write everywhere in 0x0000–0x7FFF and 0xA000–0xBFFF — under
    the sanitizer build, no error.
- [x] **Step 2–4:** fail, implement, pass.
- [x] **Step 5:** Commit `gb: cartridge chips MBC1, MBC2, MBC3, MBC5`.

### Task 5: The machine layer, the test runner on it, the first test ROMs

**Files:** create `machine/CMakeLists.txt`, `machine/machine.h`, `machine/machine.c`, `tests/unit/test_machine.c`,
`tests/gb_tests.json`; modify root `CMakeLists.txt` (add `machine`, read `gb_tests.json` into CTest tests),
`tools/common/frame_hash.h` (`frame_hash(pixels, count)`; callers pass `width * height`), `tools/common/CMakeLists.txt`,
`tools/testrunner/main.c` + its CMake, `tools/bench/main.c` + `tools/viewer/main.c` (only the `frame_hash` call),
`tools/fetch_test_roms.py`, `.gitignore` if needed.

**Interfaces — produces** (`machine.h`):

```c
typedef enum { MACHINE_NONE = 0, MACHINE_GBA, MACHINE_GB, MACHINE_GBC } machine_kind_t;
typedef struct machine machine_t;
/* By content: a GBA header (0x96 at 0xB2 and the header checksum at 0xBD right) → GBA; a Game Boy header checksum
 * at 0x14D right → GB or GBC by 0x143 bit 7; neither → by the file name's extension (.gba / .gb / .gbc, any case;
 * name may be NULL); else MACHINE_NONE. */
machine_kind_t machine_detect(const uint8_t *rom, size_t size, const char *name);
machine_t *machine_create(machine_kind_t kind);          /* NULL: no memory or MACHINE_NONE */
void machine_destroy(machine_t *m);
machine_kind_t machine_kind(const machine_t *m);          /* after load: GB vs GBC from the ROM */
int machine_load_rom(machine_t *m, const uint8_t *rom, size_t size); /* 0 ok; else MACHINE_ERR_* */
enum { MACHINE_OK = 0, MACHINE_ERR_ROM, MACHINE_ERR_UNSUPPORTED, MACHINE_ERR_NOMEM, MACHINE_ERR_STATE,
       MACHINE_ERR_SAVE };
void machine_reset(machine_t *m);
void machine_run_frame(machine_t *m);
int machine_width(const machine_t *m);   /* 240 or 160 */
int machine_height(const machine_t *m);  /* 160 or 144 */
const uint16_t *machine_framebuffer(const machine_t *m);
size_t machine_audio_read(machine_t *m, int16_t *out, size_t max_frames); /* 32768 Hz both */
void machine_set_keys(machine_t *m, uint16_t pressed);    /* GBA_KEY_* bits; L/R dropped for GB */
const char *machine_title(const machine_t *m);
uint32_t machine_crc32(const machine_t *m);
size_t machine_save_size(const machine_t *m);
size_t machine_save_write(const machine_t *m, uint8_t *buf, size_t cap);
int machine_save_read(machine_t *m, const uint8_t *data, size_t size);  /* GBA: MACHINE_ERR_SAVE on a wrong size */
int machine_save_dirty(machine_t *m);
void machine_set_rtc(machine_t *m, int64_t unix_seconds, int32_t utc_offset_s);
size_t machine_state_size(const machine_t *m);
size_t machine_state_save(const machine_t *m, void *buf, size_t cap);
int machine_state_load(machine_t *m, const void *buf, size_t len);
int machine_rewind_configure(machine_t *m, size_t budget_bytes, int interval_frames);
int machine_rewind_step_back(machine_t *m);
struct gba *machine_gba(machine_t *m);  /* the GBA core for GBA-only features (BIOS, cheats, sensors, RAM views); NULL otherwise */
struct gb *machine_gb(machine_t *m);    /* likewise */
```

State and rewind calls return `MACHINE_ERR_STATE` / 0 for Game Boy until Task 10.

Test runner: generic options (`--frames`, `--hash`, `--expect-hash`, `--screenshot`, `--wav`, `--audio-hash`,
`--expect-audio-hash`, `--save`, `--rtc`, `--keys`, `--state-at`, `--rewind-at`, `--load-state`, `--write-state`,
`--shots-every`) go through `machine_*`; GBA-only options (`--bios`, `--result-reg`, `--suite`, `--hardware`,
`--cheat`, `--notes`, the idle-loop stop) use `machine_gba()` and are a usage error for a Game Boy ROM. New, Game
Boy only:
- `--gb-serial <text>`: run until the serial output contains `<text>` → `PASS`, or contains `Failed` → `FAIL` with
  the output printed; `--frames` is the time limit (default 3600) → `TIMEOUT`.
- `--gb-memtext <text>`: blargg's newer ROMs — run until `0xA001..0xA003 == DE B0 61` and `0xA000 != 0x80`; PASS
  when `0xA000 == 0` (and the zero-terminated text at 0xA004 contains `<text>`, usually `Passed`); else FAIL
  printing the text.
- `--gb-mooneye`: run until `gb_debug_breakpoint`; PASS when `B,C,D,E,H,L == 3,5,8,13,21,34`; else FAIL; time limit
  as above.

`tests/gb_tests.json`: `{"tests":[{"name":"blargg_cpu_instrs","rom":"gb/blargg/cpu_instrs/cpu_instrs.gb",
"mode":"serial","expect":"Passed","frames":4000}, …]}` → CTest `gb_<name>`, skip code 77 when the ROM is missing.

- [x] **Step 1: Failing unit tests** (`test_machine.c`): `machine_detect_by_content` (a GBA header, a GB header, a
  GBC header, each under a wrong extension → content wins); `machine_detect_by_name` (no valid header: `x.GB`,
  `x.gbc`, `x.gba`, `x.bin`/NULL → NONE); `machine_gb_runs` (a `gbtu_rom`: size 160×144, a frame runs, title);
  `machine_gba_runs` (a tiny GBA ROM from `testutil`: 240×160); `machine_keys_drop_lr`.
- [x] **Step 2–3:** fail; implement the layer (a `switch` on kind — two cores do not need function pointers).
- [x] **Step 4: Port the tools.** `bash tools/dev.sh test` → every existing CTest test passes unchanged (hashes
  identical: the generic path must not change a GBA frame or sample).
- [x] **Step 5: Fetch.** Add to `tools/fetch_test_roms.py`: blargg's ROMs (`cpu_instrs`, `instr_timing`,
  `mem_timing`, `mem_timing-2`, `halt_bug`, `dmg_sound`, `cgb_sound`) and the mooneye test suite's prebuilt package
  into `tests/roms/gb/blargg/` and `tests/roms/gb/mooneye/`, each pinned by SHA-256 (download once, record the sum,
  download again and compare, as for Tonc). Run it.
- [x] **Step 6: First ROM tests in `gb_tests.json`:** blargg `cpu_instrs` (serial "Passed"), `instr_timing`
  (serial), `mem_timing` (serial), `mem_timing-2` (memtext), `halt_bug` (memtext); mooneye `acceptance/bits/*`,
  `acceptance/instr/daa`, `acceptance/timer/*`, `acceptance/` root instruction-timing tests (`add_sp_e_timing`,
  `call_timing`, `call_timing2`, `call_cc_timing`, `call_cc_timing2`, `jp_timing`, `jp_cc_timing`, `ld_hl_sp_e_timing`,
  `pop_timing`, `push_timing`, `ret_timing`, `ret_cc_timing`, `reti_timing`, `rst_timing`, `div_timing`,
  `ei_sequence`, `ei_timing`, `di_timing-GS`, `halt_ime0_ei`, `halt_ime0_nointr_timing`, `halt_ime1_timing`,
  `halt_ime1_timing2-GS`, `if_ie_registers`, `intr_timing`, `rapid_di_ei`, `reti_intr_timing`),
  `emulator-only/mbc1/*` except `multicart_rom_8Mb`, `emulator-only/mbc2/*`, `emulator-only/mbc5/*`.
  `bash tools/dev.sh test` → fix the core until all pass (a failing ROM = a bug to find with
  superpowers:systematic-debugging; add a unit test for each bug found). Tests that need the screen's timing
  (`intr_timing` and the `halt_*` ones wait on VBlank) may move to Task 6's list if they cannot pass yet — say so in
  PROGRESS.md.
- [x] **Step 7:** Commit `machine: one layer over both cores; the test runner runs Game Boy test ROMs`.

### Task 6: The screen (Game Boy)

**Files:** create `core_gb/src/ppu.c`, `ppu_draw.c`, `tests/unit/test_gb_ppu_timing.c`, `test_gb_ppu_draw.c`; modify
`bus.c`, `gb.c`, `gbtestutil.c` (helpers `gbtu_px(g, x, y)`, `gbtu_tile(g, index, rows[8][8])`), `gb_tests.json`,
`tests/expected_frames.json` handling if GB frames are added there (same file, `rom` under `gb/`),
`tools/fetch_test_roms.py` (dmg-acid2 + its reference picture).

**Interfaces — produces:** `ppu_tick(gb_t*)` from `gb_tick`; registers LCDC 0xFF40, STAT 41, SCY 42, SCX 43, LY 44,
LYC 45, DMA 46, BGP 47, OBP0 48, OBP1 49, WY 4A, WX 4B; VRAM 0x8000–0x9FFF, OAM 0xFE00–0xFE9F; `gb_framebuffer`.
Greys (BGR555): shade 0 `0x7FFF`, 1 `0x56B5`, 2 `0x294A`, 3 `0x0000`.

- [x] **Step 1: Failing timing tests:** a line is 456 ticks, 154 lines; modes 2 (80 ticks) → 3 (172 + extra) → 0;
  lines 144–153 mode 1; IF bit 0 at line 144's start; STAT bits 0–2 and the four interrupt sources with the "one
  line" rule (a source going high while another is high raises nothing); LY = LYC compare incl. line 153's early
  wrap to 0; VRAM unreadable (0xFF) in mode 3, OAM in modes 2–3; OAM DMA copies 160 bytes over 640 ticks while only
  HRAM is reachable; LCD off: LY 0, mode 0, no interrupts, and turning it on restarts the frame;
  `gb_lcd_off_frames_still_end` (Review Focus 4: LCDC = 0 for 5 frames then 0x91 — every `gb_run_frame` advances
  exactly 70,224 ticks and the picture after re-enabling is drawn).
- [x] **Step 2: Failing drawing tests** (pixel-exact scenes): background with scroll and wrap, both tile-data
  areas (signed addressing 0x8800), both maps; window (WX − 7, WY, its own line counter that only advances on lines
  where it showed; WX < 7 and > 166 edge cases); sprites 8×8 and 8×16, X/Y flip, palettes, behind-background
  priority, 10 per line (the first 10 in OAM order), priority by X then OAM order; BG off (LCDC bit 0) → white
  background with sprites still drawn; palettes BGP/OBP mapping.
- [x] **Step 3–4:** fail; implement; pass.
- [x] **Step 5: ROM tests.** Fetch dmg-acid2 and its reference PNG; `tools/dev.sh run …dmg-acid2.gb --frames 60
  --screenshot` → compare with the reference pixel for pixel (a small Python compare in `tools/png_probe.py` or the
  existing probe; the reference uses the greys `0xFF, 0xAA, 0x55, 0x00` — map ours to those four levels before
  comparing); then lock its frame hash in `tests/expected_frames.json`. Add to `gb_tests.json` and make pass:
  mooneye `acceptance/boot_regs-dmgABC`, `boot_div-dmgABC`, `boot_hwio-dmgABC`, `acceptance/oam_dma/*`,
  `oam_dma_restart`, `oam_dma_start`, `oam_dma_timing`, `acceptance/interrupts/ie_push`, `acceptance/serial/*`
  (if it needs DMG timing only), and anything deferred from Task 5. Run the whole `acceptance/ppu` group and record
  each result in PROGRESS.md; those that pass go into `gb_tests.json`.
- [x] **Step 6:** Commit `gb: the screen — timing and the line renderer`.

### Task 7: Sound

**Files:** create `core_gb/src/apu.c`, `tests/unit/test_gb_apu.c`; modify `bus.c`, `gb.c`, `gb_tests.json`.

**Interfaces — produces:** `apu_tick(gb_t*)`; registers 0xFF10–0xFF26, wave RAM 0xFF30–0xFF3F; `gb_audio_read`
(a buffer of at least 2 frames: 2048 stereo samples; older samples dropped).

- [x] **Step 1: Failing tests:** register read-back masks (the OR table: NR10 0x80, NR11 0x3F, NR12 0x00, NR13 0xFF,
  NR14 0xBF, NR21 0x3F … NR52 0x70; unused 0xFF); power off (NR52 bit 7 = 0) clears and locks registers (length
  counters stay writable on the DMG); square duty patterns and frequency → period in ticks = (2048 − f) × 4;
  length counters (64 / 256) incl. the extra clock when enabling length in the sequencer's first half; envelope;
  sweep (overflow check at trigger and after, the negate-then-positive quirk); wave channel period (2048 − f) × 2 and
  DMG wave-RAM read rules; noise LFSR 15 / 7 bit; sequencer steps clocked by the divider's bit 12 falling (bit 13 in
  double speed); DAC off silences and disables; NR50 / NR51 routing; `gb_audio_read` returns 32768 / 59.7275 ≈ 548–549
  samples a frame; silence = 0 after the DC filter settles.
- [x] **Step 2–4:** fail; implement; pass. Output: each output sample is the average of the mixed signal over its
  128 ticks; a one-pole high-pass per side removes the DC offset.
- [x] **Step 5: ROM tests (time-boxed: stop after the eight easiest pass and two more debugging rounds bring
  nothing):** blargg `dmg_sound` single ROMs 01–12 (memtext) — aim ≥ 8 of 12; record each in PROGRESS.md; passing
  ones into `gb_tests.json`. Lock an audio fingerprint of a small own sound ROM (built by
  `tools/build_test_roms.py` from bytes: plays each channel) in `expected_frames.json`.
- [x] **Step 6:** Commit `gb: sound`.

### Task 8: Game Boy Color

**Files:** create `core_gb/src/cgb.c`, `tests/unit/test_gb_cgb.c`, `test_gb_cgb_draw.c`; modify `bus.c`, `cpu.c`
(STOP speed switch), `ppu.c`, `ppu_draw.c`, `apu.c`, `timer.c`, `gb.c`, `gb_tests.json`, `fetch_test_roms.py`
(cgb-acid2 + reference).

**Interfaces — produces:** registers KEY1 0xFF4D, VBK 4F, HDMA1–5 51–55, RP 56 (reads as "no light"), BCPS/BCPD
68/69, OCPS/OCPD 6A/6B, OPRI 6C, SVBK 70, the undocumented 72–75; all of them read 0xFF and ignore writes when the
game runs as a plain Game Boy.

- [x] **Step 1: Failing tests:** speed switch (KEY1 bit 0 then STOP → bit 7 set, CPU cycles take 2 ticks of screen
  and sound time, the divider and timer run with the CPU; a frame is still 70,224 ticks); WRAM banks 1–7 (0 → 1) at
  0xD000 and in `gb_wram`; VRAM bank; palette RAM with auto-increment, locked in mode 3; general HDMA (the CPU stalls
  for the copy) and per-line HDMA (16 bytes each HBlank of lines 0–143, HDMA5 read-back, cancel by writing bit 7 = 0);
  drawing: tile attributes (palette, bank, X/Y flip, priority), sprites by OAM order (not X), LCDC bit 0 as "master
  priority", 8 BG + 8 OBJ palettes straight to BGR555; a plain Game Boy game still draws in the four greys;
  CGB wave-RAM rules.
- [x] **Step 2–4:** fail; implement; pass.
- [x] **Step 5: ROM tests.** cgb-acid2 against its reference picture, exact (CGB 5-bit colour → the reference's
  8-bit as `(c << 3) | (c >> 2)`); lock its hash. mooneye CGB boot tests the suite has (`misc/boot_regs-cgb`,
  `boot_div-cgbABCDE`, `boot_hwio-C` — those that fit a late Game Boy Color; record any that cannot). blargg
  `cgb_sound` 01–12 time-boxed as Task 7 (aim ≥ 8). `interrupt_time` (blargg, CGB) if it is in the package.
- [x] **Step 6:** Commit `gb: Game Boy Color`.

### Task 9: The clock and the save file

**Files:** create `core_gb/src/rtc.c`, `tests/unit/test_gb_rtc.c`, `test_gb_save.c`; modify `cart.c`, `gb.c`,
`machine.c`, `gb_tests.json`.

**Interfaces — produces:** `gb_save_size`, `gb_save_write`, `gb_save_read`, `gb_set_rtc`; MBC3 registers 0x08–0x0C
and the latch (write 0 then 1 to 0x6000–0x7FFF).

- [ ] **Step 1: Failing tests:** clock registers count from `gb_set_rtc` deltas (set t, set t + 61 → seconds 1,
  minutes 1); 59 → 0 carries up to the 9-bit day counter and its carry bit (day 511 → 0 sets bit 7 of DH, sticky);
  halt bit (DH bit 6) stops it; latch freezes the read values; writes set the live registers; a `gb_set_rtc` earlier
  than the last (the phone's clock set back) adds nothing; a jump of 10 years is handled without looping per second.
  Save: `gb_save_size` = RAM size (+ 48 with a clock; 0 without a battery); write → read round trip; the footer's
  layout (10 little-endian 32-bit register values, live then latched, then a 64-bit Unix time); reading a 44-byte
  footer; time passed since the footer's timestamp is added at the next `gb_set_rtc`;
  `gb_save_read_any_length` (Review Focus 2): lengths 0, 1, RAM − 1, RAM + 1, RAM + 20, RAM + 47, 3 × RAM, under the
  sanitizer — no error, what fits is copied, the rest of RAM is 0xFF, a partial footer is ignored.
- [ ] **Step 2–4:** fail; implement; pass.
- [ ] **Step 5: ROM tests:** mooneye has no clock tests in the pinned package unless `emulator-only/mbc3` exists —
  check; add what is there. `testrunner --save` with a Game Boy ROM writes the file through `machine_save_write`.
- [ ] **Step 6:** Commit `gb: the cartridge clock and the save file`.

### Task 10: Save states and rewind

**Files:** create `core_common/CMakeLists.txt`, `core_common/rewind_ring.c/.h`; move `core/src/delta_pack.c/.h` →
`core_common/`; modify `core/src/rewind.c` (keeps `gba_rewind_*` and `rewind_after_frame`, now thin wrappers),
`core/CMakeLists.txt`, `platform/android/CMakeLists.txt`, root `CMakeLists.txt`; create `core_gb/src/state.c`,
`tests/unit/test_gb_state.c`; modify `machine.c`, `gb.c`, root CMake (the `state_` / `rewind_` CTest variants also
for the `gb/` entries of `expected_frames.json`).

**Interfaces — produces** (`rewind_ring.h`, the ring without any core in it):

```c
typedef struct rewind_ring rewind_ring_t;
rewind_ring_t *rewind_ring_new(size_t state_size, size_t budget_bytes, int interval_frames); /* NULL: no memory */
void rewind_ring_free(rewind_ring_t *r);
void rewind_ring_clear(rewind_ring_t *r);
uint8_t *rewind_ring_begin(rewind_ring_t *r);  /* after a frame: NULL (not due yet) or a state_size buffer to fill */
void rewind_ring_commit(rewind_ring_t *r);     /* the filled buffer becomes the newest snapshot */
const uint8_t *rewind_ring_back(rewind_ring_t *r); /* the snapshot to load, or NULL when there is none */
void rewind_ring_stats(const rewind_ring_t *r, size_t *snapshots, size_t *bytes);
```

State format: magic `"GBCS"`, format version, ROM CRC-32, chunk count, then tagged versioned chunks as
`core/src/state.c` does (one chunk per component struct; the ROM is never in a state; cartridge RAM is).

- [ ] **Step 1:** Move the ring first, GBA only: `bash tools/dev.sh test` → all GBA tests pass, including the 96
  `state_` / `rewind_` tests and `oldstate_snd_mix` (the GBA state format must not change by one byte).
- [ ] **Step 2: Failing tests:** `gb_state_round_trip` (save → load into a new machine → save: identical bytes; run
  both 30 frames: identical frame and audio hashes), for a DMG and a CGB program; `gb_state_refuses`
  (Review Focus 3): wrong magic, a GBA state, another ROM's CRC, truncated at every chunk boundary, one byte short,
  a chunk length changed → `GB_ERR_STATE` and the machine's next frame is identical to an untouched twin's;
  `gb_state_bad_values_safe`: a state with ROM bank / RAM bank / VRAM bank / WRAM bank / OAM-DMA index / audio
  buffer positions set to 0xFFFF…: load is refused or the values are masked, and 10 frames run clean under the
  sanitizer; `gb_rewind_steps_back` (as `test_rewind.c` does for GBA); `machine_state_kind_mismatch` (a GBA state
  into a Game Boy machine and the reverse → `MACHINE_ERR_STATE`).
- [ ] **Step 3–4:** fail; implement; pass. `bash tools/dev.sh test` → the new `state_gb_*` / `rewind_gb_*` CTest
  variants pass too.
- [ ] **Step 5:** Commit `gb: save states and rewind; the rewind ring shared by both cores`.

### Task 11: The viewer and the speed bench

**Files:** modify `tools/viewer/main.c`, `tools/bench/main.c`, `tests/bench/scenes.txt`, `tools/dev.sh` (help text).

- [ ] **Step 1:** `gbaview` opens a Game Boy ROM through `machine_*` (window sized to the machine's picture; GBA-only
  keys do nothing). Check: `bash tools/dev.sh view tests/roms/gb/acid/dmg-acid2.gb` shows the picture; a homebrew
  game from Task 12's list plays with sound (Claude's own check: picture + a `--wav` of the same game through the
  test runner sounds right — inspected with `tools/wav_probe.py`).
- [ ] **Step 2:** `gbabench` runs scenes of both consoles through `machine_*` (GBA scenes' hashes unchanged). Add two
  Game Boy scenes to `tests/bench/scenes.txt` (the acid tests are too static: use two homebrew games from Task 12, or
  our own test ROM with scrolling and sound if none is fetched yet).
- [ ] **Step 3: The speed gate** (spec §5.7): `bash tools/dev.sh bench --runs 3` → every Game Boy scene ≥ 5× the
  slowest GBA scene's fps in the same run; GBA scenes within the 97 % floors (PROGRESS.md Notes; measure again
  before a miss counts). Record the numbers in PROGRESS.md. If the gate is missed, profile and fix before going on.
- [ ] **Step 4:** Commit `tools: the viewer and the speed bench run both consoles`.

### Task 12: The game check with homebrew games; 17a exit

**Files:** modify `tools/game_check.py`, `tools/test_game_check.py`, `tests/games/games.txt`,
`tools/fetch_test_roms.py`, docs.

- [ ] **Step 1:** `game_check.py` accepts `.gb` / `.gbc` (and a zip holding one); button names R / L / CW / CCW are
  an error for a Game Boy game; contact sheets use the machine's picture size. Tool tests first
  (`python tools/test_game_check.py`).
- [ ] **Step 2:** Pick 6–8 free homebrew games (at least 3 Game Boy, 3 Game Boy Color; open-source licences, release
  files on GitHub or similar with stable URLs), pin them in `fetch_test_roms.py` (URL, size, SHA-256) and list them
  in `tests/games/games.txt` with the standard button script. No commercial titles.
- [ ] **Step 3:** `bash tools/dev.sh games` → look at every contact sheet and listen to (probe) every sound file;
  anything wrong is a core bug: fix it test-first. Then `games --lock` each new game; `games --check` → all pass
  (31 GBA + the new ones).
- [ ] **Step 4: Exit checks:** `bash tools/dev.sh test` (all), `games --check`, `bench` (the gate and the GBA
  floors), `python tools/public_scan.py` clean, `ponytail-review` of the whole branch diff, fix what it finds.
- [ ] **Step 5: Documents:** PROGRESS.md (17a done, results table of every test-ROM group, Next = write the 17b
  plan), this plan's boxes, the spec's status line, `CLAUDE.md` key files, the parent spec §10, `docs/BUILDING.md`
  layout (`core_gb/`, `core_common/`, `machine/`), `third_party/README.md` untouched. README is **not** changed yet
  (the app cannot play these games until 17b).
- [ ] **Step 6:** Commit `milestone 17a done: the Game Boy core on the PC`. Stay on branch `milestone-17` (17b
  continues there; it merges into `main` when 1.1.0 is ready — nothing half-done lands on the public branch).

## Self-review notes

- Spec §5.1–5.8 → Tasks 1–11; §9.1 → Tasks 5–8, 11, 12; §5.6 boot values → Task 2 + mooneye boot tests (Tasks 6, 8).
- Deviation from the spec's wording: 17a ends without merging to `main` (Task 12 Step 6), so the public repo never
  shows a half-finished feature; the spec's "merged when a part is done" is read as "when a releasable part is done".
- The exact test-ROM download URLs and sums are found and pinned in Task 5 Step 5 (they cannot be known before the
  first download).
