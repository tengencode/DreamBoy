# Milestone 3b — Save Chips (SRAM, Flash, EEPROM) — Design

**Date:** 2026-09-25
**Status:** Implemented — milestone 3b complete 2026-09-25 (tag `milestone-3b`)
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§2.2 public API, §4.5 cartridge, §8 error handling, §9 testing, §10 milestones)
**Builds on:** milestone 3a (tag `milestone-3a`): sound; milestone 2: DMA; milestone 1: bus, cartridge ROM, test runner.

---

## 1. Goal and scope

Games keep their progress: emulate the three kinds of GBA save chip, work out which one a game uses, and keep save files on the PC.

**Milestone 3 split (user decisions, 2026-09-25):** 3a sound (done) → **3b save chips** (this document) → **3b-2 cartridge extras** (real-time clock, solar sensor, gyro, rumble, tilt, game database) → 3c timing accuracy.

| In scope (3b) | Out of scope |
|---|---|
| SRAM 32 KiB, Flash 64 KiB and 128 KiB, EEPROM 512 B and 8 KiB | GPIO (RTC, solar, gyro, rumble), tilt sensor, game database (3b-2) |
| Save-type detection from library marker strings; detect-on-first-use when there is none | Atmel-style page-programmed Flash; chip busy timing (erase/write complete instantly) |
| Public save API: type, bytes, size, load, dirty flag, manual override | Save states (milestone 5) |
| Save-area bus rules: byte bus, byte replication, unaligned byte lanes, mirrors | Android storage (milestone 6) |
| Test runner `--save`; `gbaview` keeps `<rom>.sav` next to the ROM | `.sav.bak` backups (Android, milestone 6) |
| jsmolka `save` ROMs (sram, flash64, flash128, none); own EEPROM test ROMs | |

---

## 2. Files

```
core/include/gba.h            + gba_save_type_t, gba_save_type(), gba_saveram(), gba_saveram_size(),
                                gba_load_saveram(), gba_saveram_dirty(), gba_set_save_type(), GBA_ERR_SAVE_SIZE
core/src/cart/save.h / save.c save state, detection, save-area (0x0E/0x0F) reads and writes, API glue
core/src/cart/flash.c         Flash command state machine
core/src/cart/eeprom.c        EEPROM serial protocol
core/src/cart/cart.h / cart.c + save_t member; detection at load; allocation of the 128 KiB save buffer
core/src/mem/bus.c            0x0D reads/writes to EEPROM when present; 0x0E/0x0F through save.c with unaligned addresses
core/src/sys/dma.c            DMA to the EEPROM area tells save.c the transfer length (size detection)
tools/testrunner/main.c       + --save <file>
tools/viewer/main.c           + <rom>.sav load at start, delayed write, write on exit (atomic)
tools/fetch_test_roms.py      + jsmolka save/sram.gba, flash64.gba, flash128.gba, none.gba
tests/own_roms/src/save_eeprom512.S, save_eeprom8k.S   own EEPROM test ROMs (+ generated .gba)
CMakeLists.txt                + jsmolka_save_* and own_save_* CTest entries (result register r12)
```

---

## 3. Save state and public API

- `save_t` (inside `cart_t`): `type`, `data` (128 KiB, allocated in `gba_load_rom` together with the ROM copy — never during frames), `size` (bytes in use for the type), `dirty`, plus Flash and EEPROM protocol state.
- Blank memory is 0xFF. `gba_reset` keeps the save data; `gba_load_rom` starts a new, blank save and re-runs detection.
- API (`core/include/gba.h`):

```c
typedef enum {
    GBA_SAVE_NONE,       /* no save chip */
    GBA_SAVE_UNKNOWN,    /* no marker string: decided on first use */
    GBA_SAVE_SRAM,       /* 32 KiB */
    GBA_SAVE_FLASH64,    /* 64 KiB */
    GBA_SAVE_FLASH128,   /* 128 KiB, two banks */
    GBA_SAVE_EEPROM,     /* EEPROM, size not known yet */
    GBA_SAVE_EEPROM512,  /* 512 B */
    GBA_SAVE_EEPROM8K,   /* 8 KiB */
} gba_save_type_t;

gba_save_type_t gba_save_type(const gba_t *g);
uint8_t *gba_saveram(gba_t *g);               /* the save bytes (valid for gba_saveram_size bytes) */
size_t gba_saveram_size(const gba_t *g);      /* 0 for NONE / UNKNOWN / EEPROM of unknown size */
gba_err_t gba_load_saveram(gba_t *g, const uint8_t *data, size_t size);
int gba_saveram_dirty(gba_t *g);              /* returns and clears the "game saved" flag */
void gba_set_save_type(gba_t *g, gba_save_type_t type); /* manual override; blanks the save */
```

- `gba_load_saveram` copies a `.sav` file into the chip. The size must match the type (SRAM 32 KiB, Flash 64/128 KiB, EEPROM 512/8192). For `GBA_SAVE_EEPROM` (size unknown) and `GBA_SAVE_UNKNOWN`, a 512 B or 8 KiB file fixes the type to that EEPROM size; a 32 KiB file fixes UNKNOWN to SRAM, 64 KiB to Flash 64K, 128 KiB to Flash 128K. Any other size returns `GBA_ERR_SAVE_SIZE` and changes nothing. Loading does not set the dirty flag.

## 4. Detection

- At load, the ROM is scanned at 4-byte-aligned offsets for these strings (first match in ROM order wins): `EEPROM_V` → EEPROM (size unknown); `SRAM_V`, `SRAM_F_V` → SRAM; `FLASH1M_V` → Flash 128K; `FLASH_V`, `FLASH512_V` → Flash 64K.
- **None found → `GBA_SAVE_UNKNOWN`** (user decision: detect on first use, as mGBA does). Reads of 0x0E/0x0F return 0xFF. The first write there decides: a byte 0xAA written to 0x0E005555 → Flash 64K (and the write is processed as a Flash command); anything else → SRAM (and the write is stored). The first DMA whose destination is in the EEPROM area → EEPROM, sized by that DMA's length (§7).

## 5. Save-area bus rules (0x0E000000–0x0FFFFFFF)

- 8-bit bus with the SRAM wait states from WAITCNT (already modelled).
- The exact (unaligned) address reaches the chip for every access size.
- **Reads:** the byte at the address; 16-bit reads return it × 0x0101, 32-bit × 0x01010101 (the CPU's rotation of misaligned loads leaves such values unchanged).
- **Writes:** one byte is written: `value >> (8 × (address & 1))` for 16-bit, `value >> (8 × (address & 3))` for 32-bit.
- **Mirrors:** 0x0F000000 mirrors 0x0E000000. Within the region, SRAM repeats every 32 KiB and Flash every 64 KiB (the address is masked).
- With no save chip (NONE, or EEPROM types) reads return 0xFF and writes are ignored.

## 6. Flash

- Command prefix: 0xAA written to 0x5555, then 0x55 to 0x2AAA (offsets within 0x0E000000), then the command byte to 0x5555:
  - 0x90 enter ID mode: reads of offset 0 / 1 return manufacturer / device — Panasonic **0x32 / 0x1B** (64K), Sanyo **0x62 / 0x13** (128K); other offsets read data. 0xF0 leaves ID mode.
  - 0x80 erase prefix; then the full prefix again and 0x10 at 0x5555 (erase chip: all 0xFF) or 0x30 at offset `n × 0x1000` (erase that 4 KiB sector of the current bank).
  - 0xA0 write: the next write stores one byte at its address in the current bank.
  - 0xB0 bank select (128K only): the next write to offset 0 selects bank 0 or 1 (bit 0).
- A write that does not fit the expected sequence resets the state machine (and is otherwise ignored). Erase and write complete instantly; erase, write and bank changes are the only ways data changes. Erase and write set the dirty flag.

## 7. EEPROM

- **Where:** 0x0D000000–0x0DFFFFFF when the ROM is 16 MiB or smaller; only 0x0DFFFF00–0x0DFFFFFF for larger ROMs (the rest of 0x0D stays ROM). Only EEPROM types map it.
- **Protocol:** one bit per access, in bit 0 of a 16-bit access (normally DMA3 halfwords). Bits arrive most significant first.
  - Read request: `1 1`, address, `0`. The next 68 reads return 4 zero bits, then the 64 data bits of the 8-byte block.
  - Write request: `1 0`, address, 64 data bits, `0`. The block is written when the final bit arrives; the dirty flag is set.
  - Address width: 6 bits (512 B: 64 blocks) or 14 bits (8 KiB: 1024 blocks; the top 4 bits are ignored).
  - Reads that are not part of a read reply return 1 ("ready": writes complete instantly).
- **Size detection:** when a DMA's destination is in the EEPROM area and the size is still unknown, its unit count decides: 9 or 73 → 512 B; 17 or 81 → 8 KiB. If the first command arrives by CPU writes instead, the size becomes 8 KiB.

## 8. PC tools

- **Test runner:** `--save <file>` loads the file with `gba_load_saveram` before the first frame (when it exists; a size error aborts with exit 3) and writes `gba_saveram()` to it after the last frame (when the size is non-zero).
- **gbaview:** the save file is the ROM path with its extension replaced by `.sav`. Loaded at start (a size error is reported and the game starts with a blank save). Each frame, `gba_saveram_dirty()` is polled; the file is written 60 frames (about 1 s) after the last frame that reported a save, and on exit if a write is pending. Writing is atomic: write `<name>.sav.tmp`, then `MoveFileExA(..., MOVEFILE_REPLACE_EXISTING)`.
- **Fetch script:** jsmolka `save/sram.gba`, `save/flash64.gba`, `save/flash128.gba`, `save/none.gba` from the pinned commit, size-checked (download approved by the user on 2026-09-25).
- **Own EEPROM ROMs** (`tests/own_roms/src/save_eeprom512.S`, `save_eeprom8k.S`, built by `tools/build_test_roms.py`; they contain `EEPROM_V`): with DMA3, write distinct 8-byte patterns to blocks 1, 2 and the last block, check the ready bit after each write, read all three back and compare, and check that block 3 (never written) reads all 0xFF. Block 0 holds a run marker: if it already holds the marker (from a loaded save), the ROM reports "found"; it then writes the marker. Result in r12 (0 = pass, else the failing check number), then `b .`. The backdrop shows the result: **green** = pass on a blank chip, **blue** = pass with the marker found, **red** = fail.

## 9. Testing

1. **Unit tests:** marker detection for each string and first-match order; UNKNOWN resolved by Flash prefix, by other write (SRAM) and by DMA (EEPROM + size); SRAM byte replication, unaligned byte lanes, 32 KiB and 0x0F mirrors; Flash ID mode (both chips), write, chip erase, sector erase, bank switch, broken sequences; EEPROM write/read for both sizes, 4 dummy bits, ready bit, DMA-length size detection, CPU-first default 8 KiB, 32 MiB-ROM window; `gba_load_saveram` for every size (accepted and rejected), dirty flag set/cleared and not set by loading, manual override, save kept across `gba_reset`.
2. **Test ROMs in CTest:** jsmolka `sram`, `flash64`, `flash128`, `none` and own `save_eeprom512`, `save_eeprom8k` with `--result-reg 12`; the own ROMs' green frames are also approved as frame fingerprints.
3. **Persistence check (user):** run `save_eeprom8k` in `gbaview` twice — green, then blue — and see an 8,192-byte `save_eeprom8k.sav` next to the ROM.
4. All milestone 1–3a tests keep passing; ASan + UBSan on.

## 10. Done when

- All unit tests pass (earlier milestones included).
- jsmolka `sram`, `flash64`, `flash128`, `none` and the two own EEPROM ROMs pass in CTest.
- The user sees green then blue in `gbaview` and the `.sav` file appears next to the ROM.
- Sanitizers report nothing.
