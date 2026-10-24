# Milestone 3b-2 — Cartridge Extras (RTC, Solar, Gyro, Rumble, Tilt, Game List) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** Emulate the real-time clock, solar sensor, gyro, rumble and tilt sensor, know which games carry them, and pass a real-game check with the user's own game A ROM.

**Architecture:** A `hardware_t` inside `cart_t` holds the extras. `hardware.c` owns the game list, the clock base (driven by emulated cycles from the time the app last set), the public API and the tilt sensor (which lives in the save area). `gpio.c` owns the 4-pin port at ROM offset 0xC4 and the devices on its pins (solar, gyro, rumble); `rtc.c` owns the S-3511 clock's serial protocol and a pure calendar routine. The bus routes ROM offsets 0xC4–0xC9 to the port; `save.c` routes 0x0E008000–0x0E0085FF to the tilt sensor.

**Tech Stack:** C11 core; Win32 for the viewer's time zone and keys.

**Spec:** `docs/superpowers/specs/2026-09-25-milestone-3b2-cart-extras-design.md`

---

## Conventions (same as before)

- Git Bash from the repo root; build/test with `bash tools/dev.sh <configure|build|unit [filter]|test|run <args>|view <args>>`.
- Unit tests: `TEST(name)` in `tests/unit/*.c`; `CHECK_EQ` compares as `unsigned long long`. Test names must not equal any function name.
- One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.
- After every task, update `docs/PROGRESS.md` and tick this plan's checkboxes (project rule in `CLAUDE.md`).
- Work on branch `milestone-3b2`.
- A running `gbaview` locks `gbaview.exe`: close viewer windows before building. The user runs PowerShell (plain `bash` there is WSL); start `gbaview` for them with the Bash tool in the background.
- **Facts used by the tests:** the GPIO port is written with 16-bit stores to 0x080000C4 (data), 0x080000C6 (direction: 1 = output from the GBA), 0x080000C8 (1 = readable). game A sends RTC commands most-significant-bit first: 0x60 reset, 0x62/0x63 control write/read, 0x64/0x65 date+time write/read, 0x67 time read (they arrive as 0x06, 0x46/0xC6, 0x26/0xA6, 0xE6). Unix times: 1790343930 = 2026-09-25 13:45:30 (Friday = 5); 1709164800 = 2024-02-29 00:00:00 (Thursday = 4); 4102444799 = 2099-12-31 23:59:59 (Thursday); 946684800 = 2000-01-01 00:00:00 (Saturday = 6); 1790380800 = 2026-09-26 00:00:00 (Saturday).

## File map

```
core/include/gba.h            + GBA_HW_* flags and the extras API
core/src/cart/hardware.h      hardware_t, rtc_t, internal functions (new)
core/src/cart/hardware.c      game list, setup/reset, clock base, API, tilt sensor (new)
core/src/cart/gpio.c          GPIO port; solar, gyro (new)
core/src/cart/rtc.c           S-3511 protocol and calendar (new)
core/src/cart/cart.h          + hardware_t hw
core/src/cart/save.c          tilt window in save_read8/save_write8
core/src/mem/bus.c            ROM offsets 0xC4-0xC9 → GPIO (reads and 16/32-bit writes)
core/src/gba.c                gba_load_rom → hardware_setup; gba_reset → hardware_reset (before the clock restarts)
tests/unit/testutil.{h,c}     + tu_rom_code()
tests/unit/test_hardware.c, test_gpio.c, test_rtc.c   (new)
tools/testrunner/main.c       + --rtc, --hardware
tools/viewer/main.c           + PC clock, extras keys, title
```

---

### Task 1: Game list, hardware state, API and the tilt sensor

**Files:**
- Create: `core/src/cart/hardware.h`, `core/src/cart/hardware.c`, `tests/unit/test_hardware.c`
- Modify: `core/include/gba.h`, `core/src/cart/cart.h`, `core/src/cart/save.c`, `core/src/gba.c`, `tests/unit/testutil.h`, `tests/unit/testutil.c`

- [x] **Step 1: Create the branch**

```bash
git checkout -b milestone-3b2
```

- [x] **Step 2: A ROM-with-game-code helper — `tests/unit/testutil.h`**

Add before `#endif`:

```c
/* A booted system whose 0x400-byte ROM starts with "b .", has game code `code` (4 letters) at 0xAC,
 * 0xEE in bytes 0xC4-0xC9 (so GPIO registers and ROM bytes can be told apart) and, when marker is
 * not NULL, that string at 0x100. */
gba_t *tu_rom_code(const char *code, const char *marker);
```

- [x] **Step 3: Append it to `tests/unit/testutil.c`**

```c
gba_t *tu_rom_code(const char *code, const char *marker) {
    uint8_t *rom = calloc(1, 0x400);
    rom[0] = 0xFE; /* b . */
    rom[1] = 0xFF;
    rom[2] = 0xFF;
    rom[3] = 0xEA;
    memcpy(rom + 0xAC, code, 4);
    memset(rom + 0xC4, 0xEE, 6);
    if (marker) memcpy(rom + 0x100, marker, strlen(marker));
    gba_t *g = gba_create(NULL);
    gba_load_rom(g, rom, 0x400);
    free(rom);
    return g;
}
```

- [x] **Step 4: Write the failing tests `tests/unit/test_hardware.c`**

```c
#include "test.h"
#include "testutil.h"

TEST(hardware_game_list_by_code) {
    static const struct {
        const char *code;
        uint32_t hw;
        gba_save_type_t save;
    } cases[] = {
        {"AXVE", GBA_HW_RTC, GBA_SAVE_FLASH128},
        {"AXPJ", GBA_HW_RTC, GBA_SAVE_FLASH128},
        {"BPEP", GBA_HW_RTC, GBA_SAVE_FLASH128},
        {"U3IE", GBA_HW_RTC | GBA_HW_SOLAR, GBA_SAVE_EEPROM},
        {"U32J", GBA_HW_RTC | GBA_HW_SOLAR, GBA_SAVE_EEPROM},
        {"U33J", GBA_HW_RTC | GBA_HW_SOLAR, GBA_SAVE_EEPROM},
        {"BR4J", GBA_HW_RTC, GBA_SAVE_FLASH64},
        {"BKAJ", GBA_HW_RTC, GBA_SAVE_FLASH128},
        {"BLJJ", GBA_HW_RTC, GBA_SAVE_FLASH64},
        {"BLVJ", GBA_HW_RTC, GBA_SAVE_FLASH64},
        {"RZWE", GBA_HW_GYRO | GBA_HW_RUMBLE, GBA_SAVE_SRAM},
        {"V49P", GBA_HW_RUMBLE, GBA_SAVE_SRAM},
        {"2GBP", GBA_HW_RUMBLE, GBA_SAVE_SRAM},
        {"KYGE", GBA_HW_TILT, GBA_SAVE_EEPROM},
        {"KHPJ", GBA_HW_TILT, GBA_SAVE_EEPROM},
        {"ZZZE", 0, GBA_SAVE_UNKNOWN},
    };
    for (size_t i = 0; i < TU_LEN(cases); i++) {
        gba_t *g = tu_rom_code(cases[i].code, NULL);
        CHECK_EQ(gba_cart_hardware(g), cases[i].hw);
        CHECK_EQ(gba_save_type(g), cases[i].save);
        gba_destroy(g);
    }
}

TEST(hardware_rtc_marker_fallback) {
    gba_t *g = tu_rom_code("ZZZE", "SIIRTC_V001");
    CHECK_EQ(gba_cart_hardware(g), GBA_HW_RTC);
    gba_destroy(g);
}

TEST(hardware_list_save_type_beats_markers) {
    gba_t *g = tu_rom_code("AXVE", "SRAM_V113");
    CHECK_EQ(gba_save_type(g), GBA_SAVE_FLASH128);
    gba_destroy(g);
}

TEST(hardware_manual_override) {
    gba_t *g = tu_rom_code("ZZZE", NULL);
    gba_set_cart_hardware(g, GBA_HW_TILT | GBA_HW_RUMBLE);
    CHECK_EQ(gba_cart_hardware(g), GBA_HW_TILT | GBA_HW_RUMBLE);
    gba_destroy(g);
}

/* game F's sampling routine: read X/Y (waiting for ready), then start the next conversion. */
static void tilt_sample(gba_t *g, uint16_t *x, uint16_t *y) {
    bus_write8(g, 0x0E008000, 0x55, false);
    bus_write8(g, 0x0E008100, 0xAA, false);
    CHECK(bus_read8(g, 0x0E008300, false) & 0x80); /* ready */
    *x = (uint16_t)((bus_read8(g, 0x0E008300, false) & 0x0F) << 8 | bus_read8(g, 0x0E008200, false));
    *y = (uint16_t)((bus_read8(g, 0x0E008500, false) & 0x0F) << 8 | bus_read8(g, 0x0E008400, false));
}

TEST(hardware_tilt_centre_and_extremes) {
    gba_t *g = tu_rom_code("KYGE", NULL);
    uint16_t x, y;
    tilt_sample(g, &x, &y);
    CHECK_EQ(x, 0x392);
    CHECK_EQ(y, 0x3A0);
    gba_set_tilt(g, 32767, -32768);
    tilt_sample(g, &x, &y);
    CHECK_EQ(x, 0x471);
    CHECK_EQ(y, 0x2C0);
    gba_set_tilt(g, -32768, 32767);
    tilt_sample(g, &x, &y);
    CHECK_EQ(x, 0x2B2);
    CHECK_EQ(y, 0x47F);
    gba_destroy(g);
}

TEST(hardware_tilt_needs_both_start_bytes) {
    gba_t *g = tu_rom_code("KYGE", NULL);
    gba_set_tilt(g, 32767, 0);
    bus_write8(g, 0x0E008100, 0xAA, false); /* no 0x55 first: no new reading */
    CHECK_EQ(bus_read8(g, 0x0E008200, false), 0x92);
    CHECK_EQ(bus_read8(g, 0x0E008000, false), 0xFF); /* not a readable register */
    gba_destroy(g);
}

TEST(hardware_tilt_only_when_fitted) {
    gba_t *g = tu_rom_code("ZZZE", NULL);
    CHECK_EQ(bus_read8(g, 0x0E008300, false), 0xFF);
    gba_destroy(g);
}
```

- [x] **Step 5: Run to verify failure**

Run: `bash tools/dev.sh unit hardware_`
Expected: build fails (`gba_cart_hardware` undeclared, `GBA_HW_RTC` unknown).

- [x] **Step 6: Public API — `core/include/gba.h`**

After the `gba_set_save_type` declaration add:

```c
enum {
    GBA_HW_RTC = 1 << 0,    /* real-time clock */
    GBA_HW_SOLAR = 1 << 1,  /* solar (light) sensor */
    GBA_HW_GYRO = 1 << 2,   /* rotation sensor */
    GBA_HW_RUMBLE = 1 << 3, /* rumble motor */
    GBA_HW_TILT = 1 << 4,   /* two-axis tilt sensor */
};

uint32_t gba_cart_hardware(const gba_t *g);           /* GBA_HW_* extras of the loaded cartridge */
void gba_set_cart_hardware(gba_t *g, uint32_t flags); /* manual override (resets the extras' state) */
/* The clock shows this time now and keeps running with emulated time (call after gba_load_rom;
 * without it the clock starts at 2000-01-01 00:00:00 when the ROM is loaded). */
void gba_set_rtc(gba_t *g, int64_t unix_seconds, int32_t utc_offset_s);
void gba_set_light(gba_t *g, uint8_t level);       /* solar sensor: 0 = dark .. 255 = full sun */
void gba_set_gyro(gba_t *g, int16_t z_rate);       /* rotation speed: > 0 clockwise, ±32767 = very fast */
void gba_set_tilt(gba_t *g, int16_t x, int16_t y); /* ±32767 = fully tilted, 0 = level */
int gba_rumble_active(const gba_t *g);             /* 1 while the rumble motor runs */
```

- [x] **Step 7: Create `core/src/cart/hardware.h`**

```c
#ifndef GBA_HARDWARE_H
#define GBA_HARDWARE_H

#include <stdbool.h>
#include <stdint.h>

#include "gba.h"

typedef struct {
    bool active;      /* a valid command byte has been received */
    bool sck;         /* SCK level at the previous pin update */
    uint8_t command;  /* low nibble 6, bits 4-6 the command, bit 7 = read */
    uint8_t bits;     /* the byte being received */
    uint8_t bit_pos;  /* next bit, 0..7 (least significant first) */
    uint8_t byte_pos; /* parameter byte index */
    uint8_t sio;      /* level the clock drives on SIO while being read */
    uint8_t control;  /* bit 6: 24-hour mode */
    uint8_t time[7];  /* latched BCD year, month, day, weekday, hour, minute, second */
} rtc_t;

typedef struct {
    uint32_t devices; /* GBA_HW_* */
    /* GPIO port */
    uint8_t data;      /* last value written to 0xC4 */
    uint8_t direction; /* 1 = output from the GBA */
    uint8_t readable;  /* 0xC8 bit 0 */
    uint8_t pins;      /* current pin levels */
    rtc_t rtc;
    /* solar sensor */
    uint16_t solar_counter;
    uint8_t solar_target;
    bool solar_clock; /* pin 0 level at the previous update */
    /* gyro */
    uint16_t gyro_shift; /* bits still to send, top bit next */
    uint8_t gyro_out;    /* the bit on pin 2 */
    bool gyro_clock;     /* pin 1 level at the previous update */
    /* tilt sensor */
    uint8_t tilt_state; /* 1 after 0x55 was written */
    uint16_t tilt_x, tilt_y;
    /* clock base and inputs set by the app */
    int64_t clock_base;   /* local seconds shown at clock_cycle */
    uint64_t clock_cycle; /* emulated cycle of clock_base */
    uint8_t light;
    int16_t gyro_rate, tilt_in_x, tilt_in_y;
} hardware_t;

/* hardware.c */
void hardware_setup(gba_t *g); /* after cart_load: extras from the game list, save-type override */
void hardware_reset(gba_t *g); /* from gba_reset, before the cycle count restarts */
int64_t hardware_local_seconds(const gba_t *g);
bool tilt_mapped(const gba_t *g, uint32_t addr);
uint8_t tilt_read(const hardware_t *h, uint32_t addr);
void tilt_write(hardware_t *h, uint32_t addr, uint8_t value);

#endif
```

- [x] **Step 8: Create `core/src/cart/hardware.c`**

```c
#include "cart/hardware.h"

#include <string.h>

#include "gba_internal.h"

#define CYCLES_PER_SECOND 16777216u
#define CLOCK_DEFAULT 946684800 /* 2000-01-01 00:00:00 */

/* Games with extras, by the first three letters of the game code (every region), after mGBA's list. */
static const struct {
    char code[4];
    uint32_t devices;
    gba_save_type_t save;
} games[] = {
    {"AXV", GBA_HW_RTC, GBA_SAVE_FLASH128},                  /* game A */
    {"AXP", GBA_HW_RTC, GBA_SAVE_FLASH128},                  /* game A */
    {"BPE", GBA_HW_RTC, GBA_SAVE_FLASH128},                  /* game A */
    {"U3I", GBA_HW_RTC | GBA_HW_SOLAR, GBA_SAVE_EEPROM},     /* game E */
    {"U32", GBA_HW_RTC | GBA_HW_SOLAR, GBA_SAVE_EEPROM},     /* game E 2 */
    {"U33", GBA_HW_RTC | GBA_HW_SOLAR, GBA_SAVE_EEPROM},     /* game E 3 */
    {"BR4", GBA_HW_RTC, GBA_SAVE_FLASH64},                   /* game J */
    {"BKA", GBA_HW_RTC, GBA_SAVE_FLASH128},                  /* game I */
    {"BLJ", GBA_HW_RTC, GBA_SAVE_FLASH64},                   /* game K */
    {"BLV", GBA_HW_RTC, GBA_SAVE_FLASH64},                   /* game K 2 */
    {"RZW", GBA_HW_GYRO | GBA_HW_RUMBLE, GBA_SAVE_SRAM},     /* game D */
    {"V49", GBA_HW_RUMBLE, GBA_SAVE_SRAM},                   /* game H */
    {"2GB", GBA_HW_RUMBLE, GBA_SAVE_SRAM},                   /* listed by mGBA with rumble */
    {"KYG", GBA_HW_TILT, GBA_SAVE_EEPROM},                   /* game F */
    {"KHP", GBA_HW_TILT, GBA_SAVE_EEPROM},                   /* game G */
};

/* The RTC library leaves its name in the ROM (word aligned), even in hacks with a changed code. */
static bool has_rtc_marker(const cart_t *c) {
    for (uint32_t off = 0; off + 8 <= c->rom_size; off += 4) {
        if (c->rom[off] == 'S' && memcmp(c->rom + off, "SIIRTC_V", 8) == 0) return true;
    }
    return false;
}

/* Everything but the clock base and the app's inputs back to power-on. */
static void reset_devices(hardware_t *h) {
    h->data = h->direction = h->readable = h->pins = 0;
    memset(&h->rtc, 0, sizeof h->rtc);
    h->rtc.control = 0x40; /* 24-hour mode, no "power lost" flag */
    h->rtc.sck = true;
    h->rtc.sio = 1;
    h->solar_counter = 0;
    h->solar_target = 0xE8;
    h->solar_clock = false;
    h->gyro_shift = 0;
    h->gyro_out = 0;
    h->gyro_clock = false;
    h->tilt_state = 0;
    h->tilt_x = 0x392;
    h->tilt_y = 0x3A0;
}

void hardware_setup(gba_t *g) {
    cart_t *c = &g->cart;
    hardware_t *h = &c->hw;
    memset(h, 0, sizeof *h);
    for (size_t k = 0; k < sizeof games / sizeof games[0]; k++) {
        if (memcmp(c->code, games[k].code, 3) == 0) {
            h->devices = games[k].devices;
            gba_set_save_type(g, games[k].save);
            break;
        }
    }
    if (!h->devices && has_rtc_marker(c)) h->devices = GBA_HW_RTC;
    reset_devices(h);
    h->clock_base = CLOCK_DEFAULT;
    h->clock_cycle = g->now;
}

int64_t hardware_local_seconds(const gba_t *g) {
    const hardware_t *h = &g->cart.hw;
    uint64_t elapsed = g->now > h->clock_cycle ? g->now - h->clock_cycle : 0;
    return h->clock_base + (int64_t)(elapsed / CYCLES_PER_SECOND);
}

void hardware_reset(gba_t *g) {
    hardware_t *h = &g->cart.hw;
    h->clock_base = hardware_local_seconds(g); /* the clock keeps running across a reset */
    h->clock_cycle = 0;                        /* gba_reset restarts the cycle count next */
    h->data = h->direction = h->readable = h->pins = 0;
    h->rtc.active = false;
    h->rtc.sck = true;
    h->rtc.bits = h->rtc.bit_pos = h->rtc.byte_pos = 0;
    h->rtc.sio = 1;
    h->solar_counter = 0;
    h->gyro_shift = 0;
    h->gyro_out = 0;
    h->tilt_state = 0;
}

/* ---- tilt sensor (0x0E008000-0x0E0085FF) ---- */

bool tilt_mapped(const gba_t *g, uint32_t addr) {
    uint32_t off = addr & 0xFFFF;
    return (g->cart.hw.devices & GBA_HW_TILT) && off >= 0x8000 && off < 0x8600;
}

static uint16_t tilt_value(uint16_t centre, int16_t input) {
    return (uint16_t)(centre + input * 0xE0 / 32768);
}

uint8_t tilt_read(const hardware_t *h, uint32_t addr) {
    switch (addr & 0xFFFF) {
    case 0x8200: return (uint8_t)h->tilt_x;
    case 0x8300: return (uint8_t)(((h->tilt_x >> 8) & 0x0F) | 0x80); /* bit 7: reading ready */
    case 0x8400: return (uint8_t)h->tilt_y;
    case 0x8500: return (uint8_t)((h->tilt_y >> 8) & 0x0F);
    default: return 0xFF;
    }
}

void tilt_write(hardware_t *h, uint32_t addr, uint8_t value) {
    uint32_t off = addr & 0xFFFF;
    if (off == 0x8000 && value == 0x55) {
        h->tilt_state = 1;
    } else if (off == 0x8100 && value == 0xAA && h->tilt_state == 1) { /* 0x55 then 0xAA: take a reading */
        h->tilt_state = 0;
        h->tilt_x = tilt_value(0x392, h->tilt_in_x);
        h->tilt_y = tilt_value(0x3A0, h->tilt_in_y);
    }
}

/* ---- public API ---- */

uint32_t gba_cart_hardware(const gba_t *g) {
    return g->cart.hw.devices;
}

void gba_set_cart_hardware(gba_t *g, uint32_t flags) {
    g->cart.hw.devices = flags & 0x1F;
    reset_devices(&g->cart.hw);
}

void gba_set_rtc(gba_t *g, int64_t unix_seconds, int32_t utc_offset_s) {
    g->cart.hw.clock_base = unix_seconds + utc_offset_s;
    g->cart.hw.clock_cycle = g->now;
}

void gba_set_light(gba_t *g, uint8_t level) {
    g->cart.hw.light = level;
}

void gba_set_gyro(gba_t *g, int16_t z_rate) {
    g->cart.hw.gyro_rate = z_rate;
}

void gba_set_tilt(gba_t *g, int16_t x, int16_t y) {
    g->cart.hw.tilt_in_x = x;
    g->cart.hw.tilt_in_y = y;
}

int gba_rumble_active(const gba_t *g) {
    const hardware_t *h = &g->cart.hw;
    return (h->devices & GBA_HW_RUMBLE) && (h->direction & h->data & 8);
}
```

- [x] **Step 9: The cartridge holds it — `core/src/cart/cart.h`**

Add `#include "cart/hardware.h"` after `#include "cart/save.h"`, and add the member `hardware_t hw;` after `save_t save;`.

- [x] **Step 10: Set up at load, re-base at reset — `core/src/gba.c`**

Replace `gba_load_rom`'s body line `if (err == GBA_OK) gba_reset(g);` with:

```c
    if (err == GBA_OK) {
        hardware_setup(g);
        gba_reset(g);
    }
```

In `gba_reset`, add as the first line (before `g->now = 0;`):

```c
    hardware_reset(g); /* before the cycle count restarts: it re-bases the clock */
```

- [x] **Step 11: The tilt window — `core/src/cart/save.c`**

In `save_read8`, add as the first line:

```c
    if (tilt_mapped(g, addr)) return tilt_read(&g->cart.hw, addr);
```

In `save_write8`, add as the first lines:

```c
    if (tilt_mapped(g, addr)) {
        tilt_write(&g->cart.hw, addr, value);
        return;
    }
```

- [x] **Step 12: Run tests to verify they pass**

Run: `bash tools/dev.sh configure` then `bash tools/dev.sh unit` and `bash tools/dev.sh test`
Expected: every unit test passes (247 earlier + 7 `hardware_*`), no warnings; CTest `100% tests passed` (43).

- [x] **Step 13: Commit**

```bash
git add core tests/unit
git commit -m "feat(cart): add the extras game list, clock base, API and tilt sensor" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: GPIO port, solar sensor, gyro and rumble

**Files:**
- Create: `core/src/cart/gpio.c`, `tests/unit/test_gpio.c`
- Modify: `core/src/cart/hardware.h`, `core/src/mem/bus.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_gpio.c`**

```c
#include "test.h"
#include "testutil.h"

static void port(gba_t *g, uint32_t reg, uint16_t value) {
    bus_write16(g, 0x08000000 + reg, value, false);
}

static uint16_t pins(gba_t *g) {
    return (uint16_t)bus_read16(g, 0x080000C4, false);
}

TEST(gpio_registers_readable_only_when_enabled) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    CHECK_EQ(bus_read16(g, 0x080000C6, false), 0xEEEE); /* write-only: the ROM's bytes */
    port(g, 0xC6, 0x0005);
    port(g, 0xC8, 0x0001);
    CHECK_EQ(bus_read16(g, 0x080000C6, false), 0x0005);
    CHECK_EQ(bus_read16(g, 0x080000C8, false), 0x0001);
    CHECK_EQ(bus_read32(g, 0x080000C4, false) >> 16, 0x0005);
    CHECK_EQ(bus_read16(g, 0x0A0000C6, false), 0x0005); /* every ROM mirror */
    port(g, 0xC8, 0x0000);
    CHECK_EQ(bus_read16(g, 0x080000C6, false), 0xEEEE);
    gba_destroy(g);
}

TEST(gpio_ignores_byte_writes) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    bus_write8(g, 0x080000C8, 1, false);
    CHECK_EQ(bus_read16(g, 0x080000C8, false), 0xEEEE);
    gba_destroy(g);
}

TEST(gpio_absent_without_port_devices) {
    gba_t *g = tu_rom_code("KYGE", NULL); /* tilt only: no GPIO port */
    port(g, 0xC8, 0x0001);
    CHECK_EQ(bus_read16(g, 0x080000C8, false), 0xEEEE);
    gba_destroy(g);
}

/* GBATEK's solar routine: reset the counter, then clock it until pin 3 answers. */
static int solar_clocks(gba_t *g) {
    port(g, 0xC8, 1);
    port(g, 0xC6, 7); /* pins 0-2 out, pin 3 in */
    port(g, 0xC4, 2); /* reset */
    port(g, 0xC4, 0);
    for (int n = 1; n <= 0x100; n++) {
        port(g, 0xC4, 1);
        port(g, 0xC4, 0);
        if (pins(g) & 8) return n;
    }
    return -1;
}

TEST(gpio_solar_sensor_levels) {
    gba_t *g = tu_rom_code("U3IE", NULL);
    CHECK_EQ(solar_clocks(g), 0xE8); /* dark */
    gba_set_light(g, 255);
    CHECK_EQ(solar_clocks(g), 0x50); /* full sun */
    gba_set_light(g, 128);
    CHECK_EQ(solar_clocks(g), 0x9C);
    gba_destroy(g);
}

/* GBATEK's gyro routine: start a conversion, then clock out 16 bits (4 zeros + 12 data). */
static uint16_t gyro_value(gba_t *g) {
    port(g, 0xC8, 1);
    port(g, 0xC6, 0x0B); /* pin 2 in, the rest out */
    port(g, 0xC4, 3);    /* start, clock high */
    port(g, 0xC4, 2);
    uint16_t v = 0;
    for (int i = 0; i < 16; i++) {
        uint16_t bit = (pins(g) >> 2) & 1;
        port(g, 0xC4, 0); /* clock low: the next bit */
        port(g, 0xC4, 2);
        v = (uint16_t)(v << 1 | bit);
    }
    return v;
}

TEST(gpio_gyro_reports_rotation) {
    gba_t *g = tu_rom_code("RZWE", NULL);
    CHECK_EQ(gyro_value(g), 0x06C0); /* still */
    gba_set_gyro(g, 16384);
    CHECK_EQ(gyro_value(g), 0x0880); /* clockwise */
    gba_set_gyro(g, -16384);
    CHECK_EQ(gyro_value(g), 0x0500);
    gba_set_gyro(g, 32767);
    CHECK_EQ(gyro_value(g), 0x0A3F);
    gba_destroy(g);
}

TEST(gpio_rumble_follows_pin_3) {
    gba_t *g = tu_rom_code("V49E", NULL);
    CHECK_EQ(gba_rumble_active(g), 0);
    port(g, 0xC6, 8);
    port(g, 0xC4, 8);
    CHECK_EQ(gba_rumble_active(g), 1);
    port(g, 0xC4, 0);
    CHECK_EQ(gba_rumble_active(g), 0);
    port(g, 0xC4, 8);
    port(g, 0xC6, 0); /* pin 3 as an input: off */
    CHECK_EQ(gba_rumble_active(g), 0);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit gpio_`
Expected: 4 of the 6 `gpio_*` tests FAIL (the ROM area ignores writes; reads give ROM bytes); `gpio_ignores_byte_writes` and `gpio_absent_without_port_devices` already pass, because they check that nothing happens.

- [x] **Step 3: Declare the port — `core/src/cart/hardware.h`**

Add before `#endif`:

```c
/* gpio.c: ROM offsets 0xC4-0xC9 in regions 0x08-0x0D. */
bool gpio_mapped(const gba_t *g, uint32_t addr);
uint32_t gpio_read(gba_t *g, uint32_t addr, unsigned size);
void gpio_write(gba_t *g, uint32_t addr, uint32_t value, unsigned size);
```

- [x] **Step 4: Create `core/src/cart/gpio.c`**

```c
#include "cart/hardware.h"

#include "gba_internal.h"

enum { GPIO_DATA = 0xC4, GPIO_DIRECTION = 0xC6, GPIO_CONTROL = 0xC8 };

bool gpio_mapped(const gba_t *g, uint32_t addr) {
    uint32_t off = addr & 0x01FFFFFF;
    uint32_t port_devices = GBA_HW_RTC | GBA_HW_SOLAR | GBA_HW_GYRO | GBA_HW_RUMBLE;
    return (g->cart.hw.devices & port_devices) && off >= GPIO_DATA && off < GPIO_DATA + 6;
}

/* ---- devices on the pins; each returns the pins it drives ---- */

/* Solar sensor: pin 1 resets a counter, pin 0 rising edges count, pin 3 answers once the counter
 * reaches a target set by the light level (0xE8 in the dark .. 0x50 in full sun). */
static uint8_t solar_pins(hardware_t *h, uint8_t out) {
    if (out & 4) return 0; /* the RTC's chip select: not for the sensor */
    bool clock = (out & 1) != 0;
    if (out & 2) {
        h->solar_counter = 0;
        h->solar_target = (uint8_t)(0xE8 - h->light * (0xE8 - 0x50) / 255);
    } else if (clock && !h->solar_clock) {
        h->solar_counter++;
    }
    h->solar_clock = clock;
    return h->solar_counter >= h->solar_target ? 8 : 0;
}

static void gyro_next_bit(hardware_t *h) {
    h->gyro_out = (uint8_t)(h->gyro_shift >> 15);
    h->gyro_shift = (uint16_t)(h->gyro_shift << 1);
}

/* Gyro: pin 0 takes a sample (4 zero bits + 12 data bits); each falling edge on pin 1 puts the
 * next bit on pin 2. */
static uint8_t gyro_pins(hardware_t *h, uint8_t out) {
    bool clock = (out & 2) != 0;
    if (out & 1) {
        int32_t v = 0x6C0 + h->gyro_rate * 0x380 / 32768;
        if (v < 0) v = 0;
        if (v > 0xFFF) v = 0xFFF;
        h->gyro_shift = (uint16_t)v;
        gyro_next_bit(h);
    } else if (h->gyro_clock && !clock) {
        gyro_next_bit(h);
    }
    h->gyro_clock = clock;
    return (uint8_t)(h->gyro_out << 2);
}

/* After every change: the GBA drives its output pins, the devices drive the rest. */
static void update(gba_t *g) {
    hardware_t *h = &g->cart.hw;
    uint8_t out = h->data & h->direction;
    uint8_t driven = 0;
    if (h->devices & GBA_HW_SOLAR) driven |= solar_pins(h, out);
    if (h->devices & GBA_HW_GYRO) driven |= gyro_pins(h, out);
    h->pins = (uint8_t)(out | (driven & ~h->direction & 0xF));
}

/* ---- registers ---- */

static uint16_t reg16(gba_t *g, uint32_t addr) {
    const hardware_t *h = &g->cart.hw;
    uint32_t off = addr & 0x01FFFFFE;
    if (!h->readable || off < GPIO_DATA || off > GPIO_CONTROL) return (uint16_t)cart_rom_read(&g->cart, addr & ~1u, 2);
    if (off == GPIO_DATA) return h->pins;
    if (off == GPIO_DIRECTION) return h->direction;
    return h->readable;
}

uint32_t gpio_read(gba_t *g, uint32_t addr, unsigned size) {
    if (size == 4) return reg16(g, addr) | (uint32_t)reg16(g, addr + 2) << 16;
    uint16_t v = reg16(g, addr);
    return size == 2 ? v : (uint32_t)(v >> ((addr & 1) * 8)) & 0xFF;
}

static void write_reg(gba_t *g, uint32_t off, uint16_t v) {
    hardware_t *h = &g->cart.hw;
    switch (off) {
    case GPIO_DATA:
        h->data = v & 0xF;
        update(g);
        break;
    case GPIO_DIRECTION:
        h->direction = v & 0xF;
        update(g);
        break;
    case GPIO_CONTROL: h->readable = v & 1; break;
    default: break;
    }
}

void gpio_write(gba_t *g, uint32_t addr, uint32_t value, unsigned size) {
    if (size == 1) return; /* the ROM bus takes no byte writes */
    uint32_t off = addr & 0x01FFFFFE;
    write_reg(g, off, (uint16_t)value);
    if (size == 4) write_reg(g, off + 2, (uint16_t)(value >> 16));
}
```

- [x] **Step 5: Route the port — `core/src/mem/bus.c`**

In `mem_read`, replace

```c
    case 0xC: return cart_rom_read(&g->cart, addr, size);
    case 0xD:
        if (save_eeprom_mapped(g, addr)) return save_eeprom_read(g);
        return cart_rom_read(&g->cart, addr, size);
```

with

```c
    case 0xC:
    case 0xD:
        if ((addr >> 24) == 0xD && save_eeprom_mapped(g, addr)) return save_eeprom_read(g);
        if (gpio_mapped(g, addr)) return gpio_read(g, addr, size);
        return cart_rom_read(&g->cart, addr, size);
```

The `case 0x8:` … `case 0xB:` labels just above already fall through into this block, so regions 0x08–0x0D all check the GPIO port.

In `mem_write`, replace

```c
    case 0xD:
        if (save_eeprom_mapped(g, addr)) save_eeprom_write(g, v); /* the bit is in bit 0 */
        break;
```

with

```c
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD:
        if ((addr >> 24) == 0xD && save_eeprom_mapped(g, addr)) save_eeprom_write(g, v); /* bit 0 */
        else if (gpio_mapped(g, addr)) gpio_write(g, addr, v, size);
        break;
```

- [x] **Step 6: Run tests to verify they pass**

Run: `bash tools/dev.sh configure` then `bash tools/dev.sh unit` and `bash tools/dev.sh test`
Expected: every unit test passes (including the 6 `gpio_*`), CTest `100% tests passed` (43).

- [x] **Step 7: Commit**

```bash
git add core tests/unit/test_gpio.c
git commit -m "feat(cart): add the GPIO port with solar sensor, gyro and rumble" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Real-time clock

**Files:**
- Create: `core/src/cart/rtc.c`, `tests/unit/test_rtc.c`
- Modify: `core/src/cart/hardware.h`, `core/src/cart/gpio.c` (`update`)

- [x] **Step 1: Write the failing tests `tests/unit/test_rtc.c`**

```c
#include "test.h"
#include "testutil.h"

static void port(gba_t *g, uint32_t reg, uint16_t value) {
    bus_write16(g, 0x08000000 + reg, value, false);
}

/* game A's routines. Pins: 0 SCK, 1 SIO, 2 CS. */
static void rtc_begin(gba_t *g) {
    port(g, 0xC8, 1);
    port(g, 0xC6, 7);
    port(g, 0xC4, 1); /* SCK high, CS low */
    port(g, 0xC4, 5); /* CS high */
}

static void rtc_end(gba_t *g) {
    port(g, 0xC4, 1);
}

static void rtc_send(gba_t *g, uint8_t value, bool msb_first) {
    port(g, 0xC6, 7);
    for (int i = 0; i < 8; i++) {
        uint16_t bit = (value >> (msb_first ? 7 - i : i)) & 1;
        port(g, 0xC4, (uint16_t)(bit << 1 | 4)); /* SCK low, SIO = bit */
        port(g, 0xC4, (uint16_t)(bit << 1 | 5)); /* SCK high: the clock takes the bit */
    }
}

static uint8_t rtc_receive(gba_t *g) {
    port(g, 0xC6, 5); /* SIO in */
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) {
        port(g, 0xC4, 4); /* SCK low: the clock puts out the next bit */
        port(g, 0xC4, 5);
        v |= (uint8_t)(((bus_read16(g, 0x080000C4, false) >> 1) & 1) << i);
    }
    return v;
}

/* Sends a command (as game A: most significant bit first) and reads n bytes. */
static void rtc_read(gba_t *g, uint8_t command, uint8_t *out, int n) {
    rtc_begin(g);
    rtc_send(g, command, true);
    for (int i = 0; i < n; i++) out[i] = rtc_receive(g);
    rtc_end(g);
}

static void check_bytes(const uint8_t *got, const uint8_t *want, int n) {
    for (int i = 0; i < n; i++) CHECK_EQ(got[i], want[i]);
}

TEST(rtc_reads_date_and_time) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    gba_set_rtc(g, 1790343930, 0); /* 2026-09-25 13:45:30, a Friday */
    uint8_t t[7];
    static const uint8_t want[7] = {0x26, 0x09, 0x25, 0x05, 0x13, 0x45, 0x30};
    rtc_read(g, 0x65, t, 7);
    check_bytes(t, want, 7);
    gba_destroy(g);
}

TEST(rtc_time_command_and_utc_offset) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    gba_set_rtc(g, 1790343930, 3600); /* one hour ahead of UTC */
    uint8_t t[3];
    static const uint8_t want[3] = {0x14, 0x45, 0x30};
    rtc_read(g, 0x67, t, 3);
    check_bytes(t, want, 3);
    gba_destroy(g);
}

TEST(rtc_control_register_and_12_hour_mode) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    gba_set_rtc(g, 1790343930, 0);
    uint8_t c, t[7];
    rtc_read(g, 0x63, &c, 1);
    CHECK_EQ(c, 0x40); /* 24-hour mode, healthy */
    rtc_begin(g);
    rtc_send(g, 0x62, true);  /* control write */
    rtc_send(g, 0x00, false); /* 12-hour mode */
    rtc_end(g);
    rtc_read(g, 0x63, &c, 1);
    CHECK_EQ(c, 0x00);
    rtc_read(g, 0x65, t, 7);
    CHECK_EQ(t[4], 0x01); /* 13:45 → 1 */
    gba_destroy(g);
}

TEST(rtc_reset_command_clears_control) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    uint8_t c;
    rtc_begin(g);
    rtc_send(g, 0x60, true);
    rtc_end(g);
    rtc_read(g, 0x63, &c, 1);
    CHECK_EQ(c, 0x00);
    gba_destroy(g);
}

TEST(rtc_ignores_bad_commands) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    uint8_t v;
    rtc_read(g, 0x00, &v, 1); /* not 0110 in the low nibble */
    CHECK_EQ(v, 0xFF);        /* SIO stays high */
    gba_destroy(g);
}

TEST(rtc_chip_select_low_aborts) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    gba_set_rtc(g, 1790343930, 0);
    rtc_begin(g);
    port(g, 0xC4, 4); /* half a command ... */
    port(g, 0xC4, 5);
    port(g, 0xC4, 6);
    port(g, 0xC4, 7);
    rtc_end(g); /* ... then CS low */
    uint8_t t[7];
    static const uint8_t want[7] = {0x26, 0x09, 0x25, 0x05, 0x13, 0x45, 0x30};
    rtc_read(g, 0x65, t, 7);
    check_bytes(t, want, 7);
    gba_destroy(g);
}

TEST(rtc_keeps_time_with_emulated_seconds) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    gba_set_rtc(g, 1790343930, 0);
    g->now += 61ull * 16777216; /* 61 emulated seconds */
    uint8_t t[3];
    static const uint8_t want[3] = {0x13, 0x46, 0x31};
    rtc_read(g, 0x67, t, 3);
    check_bytes(t, want, 3);
    gba_reset(g); /* a reset does not stop the clock */
    g->now += 16777216;
    static const uint8_t after[3] = {0x13, 0x46, 0x32};
    rtc_read(g, 0x67, t, 3);
    check_bytes(t, after, 3);
    gba_destroy(g);
}

TEST(rtc_defaults_to_the_year_2000) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    uint8_t t[7];
    static const uint8_t want[7] = {0x00, 0x01, 0x01, 0x06, 0x00, 0x00, 0x00};
    rtc_read(g, 0x65, t, 7);
    check_bytes(t, want, 7);
    gba_destroy(g);
}

TEST(rtc_calendar_edges) {
    static const struct {
        int64_t t;
        uint8_t want[7];
    } cases[] = {
        {1709164800, {0x24, 0x02, 0x29, 0x04, 0x00, 0x00, 0x00}}, /* leap day 2024 */
        {4102444799, {0x99, 0x12, 0x31, 0x04, 0x23, 0x59, 0x59}}, /* last second of 2099 */
        {1790380800, {0x26, 0x09, 0x26, 0x06, 0x00, 0x00, 0x00}}, /* midnight, Saturday */
    };
    for (size_t i = 0; i < TU_LEN(cases); i++) {
        uint8_t out[7];
        rtc_calendar(cases[i].t, true, out);
        check_bytes(out, cases[i].want, 7);
    }
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit rtc_`
Expected: build fails (`rtc_calendar` undeclared).

- [x] **Step 3: Declare the clock — `core/src/cart/hardware.h`**

Add before `#endif`:

```c
/* rtc.c */
void rtc_calendar(int64_t local_seconds, bool hour24, uint8_t out[7]); /* BCD date+time */
uint8_t rtc_pins(rtc_t *r, uint8_t out, int64_t local_seconds);       /* returns the SIO level << 1 */
```

- [x] **Step 4: Create `core/src/cart/rtc.c`**

```c
#include "cart/hardware.h"

/* Seiko S-3511, as wired on GBA cartridges: pins 0 SCK, 1 SIO, 2 CS. */
enum { RTC_RESET = 0, RTC_DATETIME = 2, RTC_FORCE_IRQ = 3, RTC_CONTROL = 4, RTC_TIME = 6 };

static uint8_t bcd(unsigned v) {
    return (uint8_t)((v / 10 % 10) << 4 | v % 10);
}

/* Days since 1970-01-01 to year/month/day (Howard Hinnant's days_to_civil). */
static void civil_from_days(int64_t z, int64_t *year, unsigned *month, unsigned *day) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *day = doy - (153 * mp + 2) / 5 + 1;
    *month = mp < 10 ? mp + 3 : mp - 9;
    *year = (int64_t)yoe + era * 400 + (*month <= 2);
}

void rtc_calendar(int64_t t, bool hour24, uint8_t out[7]) {
    int64_t days = t / 86400, secs = t % 86400;
    if (secs < 0) {
        secs += 86400;
        days--;
    }
    int64_t year;
    unsigned month, day;
    civil_from_days(days, &year, &month, &day);
    unsigned hour = (unsigned)(secs / 3600);
    out[0] = bcd((unsigned)(((year % 100) + 100) % 100));
    out[1] = bcd(month);
    out[2] = bcd(day);
    out[3] = bcd((unsigned)(((days % 7) + 11) % 7)); /* 1970-01-01 was a Thursday; 0 = Sunday */
    out[4] = bcd(hour24 ? hour : hour % 12);
    out[5] = bcd((unsigned)(secs / 60 % 60));
    out[6] = bcd((unsigned)(secs % 60));
}

static void end_transfer(rtc_t *r) {
    r->active = false;
    r->sck = true;
    r->bits = 0;
    r->bit_pos = 0;
    r->byte_pos = 0;
    r->sio = 1;
}

/* A command byte: low nibble 0110, bits 4-6 the command, bit 7 = read. Anything else is ignored. */
static void begin(rtc_t *r, uint8_t byte, int64_t now) {
    if ((byte & 0x0F) != 6) return;
    r->active = true;
    r->command = byte;
    r->byte_pos = 0;
    uint8_t cmd = (byte >> 4) & 7;
    if (cmd == RTC_RESET) r->control = 0;
    if (cmd == RTC_DATETIME || cmd == RTC_TIME) rtc_calendar(now, (r->control & 0x40) != 0, r->time);
}

static uint8_t param(const rtc_t *r) {
    switch ((r->command >> 4) & 7) {
    case RTC_CONTROL: return r->control;
    case RTC_DATETIME: return r->time[r->byte_pos % 7];
    case RTC_TIME: return r->time[4 + r->byte_pos % 3];
    default: return 0xFF;
    }
}

uint8_t rtc_pins(rtc_t *r, uint8_t out, int64_t now) {
    bool sck = (out & 1) != 0;
    if (!(out & 4)) { /* CS low: no transfer */
        end_transfer(r);
        return 2;
    }
    if (r->active && (r->command & 0x80)) { /* reading: a bit out on each falling edge, lowest first */
        if (r->sck && !sck) {
            r->sio = (uint8_t)((param(r) >> r->bit_pos) & 1);
            if (++r->bit_pos == 8) {
                r->bit_pos = 0;
                r->byte_pos++;
            }
        }
        r->sck = sck;
        return (uint8_t)(r->sio << 1);
    }
    if (!r->sck && sck) { /* receiving: a bit in on each rising edge, lowest first */
        r->bits = (uint8_t)(r->bits | ((out >> 1) & 1) << r->bit_pos);
        if (++r->bit_pos == 8) {
            if (!r->active) {
                begin(r, r->bits, now);
            } else {
                if (((r->command >> 4) & 7) == RTC_CONTROL) r->control = r->bits; /* date/time writes are ignored */
                r->byte_pos++;
            }
            r->bits = 0;
            r->bit_pos = 0;
        }
    }
    r->sck = sck;
    return 2; /* SIO idles high */
}
```

- [x] **Step 5: Put the clock on the port — `core/src/cart/gpio.c`**

In `update`, add before the solar line:

```c
    if (h->devices & GBA_HW_RTC) driven |= rtc_pins(&h->rtc, out, hardware_local_seconds(g));
```

- [x] **Step 6: Run tests to verify they pass**

Run: `bash tools/dev.sh configure` then `bash tools/dev.sh unit` and `bash tools/dev.sh test`
Expected: every unit test passes (including the 9 `rtc_*`), CTest `100% tests passed` (43).

- [x] **Step 7: Commit**

```bash
git add core tests/unit/test_rtc.c
git commit -m "feat(cart): add the S-3511 real-time clock" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Test-runner options

**Files:**
- Modify: `tools/testrunner/main.c`

- [x] **Step 1: Usage text**

In the top comment, add after the `--save` line:

```c
 * --rtc <unix seconds> sets the cartridge clock; --hardware <rtc,solar,gyro,rumble,tilt|none> forces
 * the cartridge extras.
```

In `usage()`, add a line to the message after the `--wav` line:

```c
                    "                  [--rtc <unix seconds>] [--hardware <rtc,solar,gyro,rumble,tilt|none>]\n"
```

- [x] **Step 2: Parse the extras list — add before `static int usage(void)`**

```c
/* "rtc,gyro" → GBA_HW_RTC | GBA_HW_GYRO; "none" → 0. False for an unknown name. */
static bool parse_hardware(const char *list, uint32_t *flags) {
    static const struct {
        const char *name;
        uint32_t flag;
    } names[] = {{"rtc", GBA_HW_RTC}, {"solar", GBA_HW_SOLAR}, {"gyro", GBA_HW_GYRO},
                 {"rumble", GBA_HW_RUMBLE}, {"tilt", GBA_HW_TILT}};
    char buf[128];
    *flags = 0;
    if (!strcmp(list, "none")) return true;
    snprintf(buf, sizeof buf, "%s", list);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        size_t k = 0;
        while (k < sizeof names / sizeof names[0] && strcmp(tok, names[k].name) != 0) k++;
        if (k == sizeof names / sizeof names[0]) return false;
        *flags |= names[k].flag;
    }
    return true;
}
```

- [x] **Step 3: The options in `main`**

After `const char *wav_path = NULL, *expect_audio = NULL, *save_path = NULL;` add:

```c
    const char *hardware = NULL;
    bool rtc_given = false;
    long long rtc_seconds = 0;
```

Add to the option chain, before the `else if (argv[i][0] != '-' && !rom_path)` line:

```c
        else if (!strcmp(argv[i], "--rtc") && i + 1 < argc) {
            rtc_seconds = strtoll(argv[++i], NULL, 0);
            rtc_given = true;
        } else if (!strcmp(argv[i], "--hardware") && i + 1 < argc) hardware = argv[++i];
```

Replace `if (!rom_path || result_reg > 15 || frames <= 0) return usage();` with:

```c
    uint32_t hw_flags = 0;
    if (!rom_path || result_reg > 15 || frames <= 0) return usage();
    if (hardware && !parse_hardware(hardware, &hw_flags)) return usage();
```

After the `gba_load_rom` check (before `if (save_path) {`) add:

```c
    if (hardware) gba_set_cart_hardware(g, hw_flags);
    if (rtc_given) gba_set_rtc(g, rtc_seconds, 0);
```

- [x] **Step 4: Check**

```bash
bash tools/dev.sh run tests/roms/jsmolka/arm.gba --result-reg 12 --hardware rtc,gyro --rtc 1790343930
bash tools/dev.sh run tests/roms/jsmolka/arm.gba --result-reg 12 --hardware sonar; echo "exit=$?"
bash tools/dev.sh test
```

Expected: `PASS ...arm.gba`; the unknown name prints the usage text and `exit=3`; CTest `100% tests passed` (43).

- [x] **Step 5: Commit**

```bash
git add tools/testrunner/main.c
git commit -m "feat(tools): set the cartridge clock and extras from the test runner" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Extras in `gbaview`

**Files:**
- Modify: `tools/viewer/main.c`

- [x] **Step 1: Documentation and state**

In the top comment, after the Keys line add:

```c
 * Cartridge extras: I/J/K/L tilt, Q/E turn (gyro), [ and ] light -/+ (solar); the clock is set
 * from the PC's local time. The title shows the light level (solar carts) and RUMBLE.
```

Add `#include <time.h>` after `#include <string.h>`, and after the `static int save_countdown;` line:

```c
static uint8_t extra_held; /* I, J, K, L, Q, E held: bits 0-5 */
static int light_level;    /* 0..255 */
```

- [x] **Step 2: Helpers — add before `/* ---- window ---- */`**

```c
/* ---- cartridge extras ---- */

static uint8_t extra_bit(WPARAM vk) {
    switch (vk) {
    case 'I': return 1;
    case 'J': return 2;
    case 'K': return 4;
    case 'L': return 8;
    case 'Q': return 16;
    case 'E': return 32;
    default: return 0;
    }
}

static void apply_extras(void) {
    int x = ((extra_held & 8) ? 24000 : 0) - ((extra_held & 2) ? 24000 : 0);
    int y = ((extra_held & 4) ? 24000 : 0) - ((extra_held & 1) ? 24000 : 0);
    int turn = ((extra_held & 32) ? 16000 : 0) - ((extra_held & 16) ? 16000 : 0);
    gba_set_tilt(gba, (int16_t)x, (int16_t)y);
    gba_set_gyro(gba, (int16_t)turn);
}

static void update_title(HWND hwnd) {
    static char shown[128];
    char title[128];
    int n = snprintf(title, sizeof title, "gbaview - %s", gba_rom_title(gba));
    if (n > 0 && (gba_cart_hardware(gba) & GBA_HW_SOLAR)) {
        n += snprintf(title + n, sizeof title - (size_t)n, "  light %d", light_level);
    }
    if (n > 0 && gba_rumble_active(gba)) snprintf(title + n, sizeof title - (size_t)n, "  RUMBLE");
    if (strcmp(title, shown) != 0) {
        SetWindowTextA(hwnd, title);
        snprintf(shown, sizeof shown, "%s", title);
    }
}

/* Seconds to add to UTC for the PC's local time (daylight saving included). */
static int32_t local_utc_offset(void) {
    TIME_ZONE_INFORMATION tz;
    DWORD mode = GetTimeZoneInformation(&tz);
    LONG bias = tz.Bias;
    if (mode == TIME_ZONE_ID_DAYLIGHT) bias += tz.DaylightBias;
    else if (mode == TIME_ZONE_ID_STANDARD) bias += tz.StandardBias;
    return (int32_t)(-bias * 60);
}
```

- [x] **Step 3: Keys — `window_proc`**

In `WM_KEYDOWN`, add after the `VK_F12` block:

```c
        if (wp == VK_OEM_4 || wp == VK_OEM_6) { /* [ and ]: darker / brighter */
            light_level += wp == VK_OEM_6 ? 32 : -32;
            if (light_level < 0) light_level = 0;
            if (light_level > 255) light_level = 255;
            gba_set_light(gba, (uint8_t)light_level);
            return 0;
        }
        extra_held |= extra_bit(wp);
```

In `WM_KEYUP`, add as the first line:

```c
        extra_held &= (uint8_t)~extra_bit(wp);
```

- [x] **Step 4: Use them in `main`**

After `load_save();` add:

```c
    gba_set_rtc(gba, (int64_t)time(NULL), local_utc_offset());
```

Before `gba_run_frame(gba);` in the main loop add:

```c
        apply_extras();
```

After `UpdateWindow(hwnd);` in the main loop add:

```c
        update_title(hwnd);
```

- [x] **Step 5: Build and self-test**

```bash
bash tools/dev.sh view tests/roms/jsmolka/hello.gba --mute --autotest 60 build/shots/view.png
```

Expected: builds without warnings; a window for about a second; `60 frames in 1.0x s`; `saved build/shots/view.png`.

- [x] **Step 6: Commit**

```bash
git add tools/viewer/main.c
git commit -m "feat(tools): PC clock and keys for tilt, gyro and light in gbaview" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: game A check and milestone 3b-2 exit

**Files:**
- Modify: `docs/PROGRESS.md`, this plan's checkboxes, the 3b-2 spec's status line, `CLAUDE.md`

- [x] **Step 1: A quick headless look**

Ask the user for the full path of their game A ROM (it stays where it is; never copy it into the repository). Then:

```bash
bash tools/dev.sh run "<path>" --frames 600 --rtc 1790343930 --screenshot build/shots/game_a.png --hash
```

Expected: no crash or sanitizer report; open `build/shots/game_a.png` (Read tool) — the game's intro or title screen. (It creates no save file: the runner writes saves only with `--save`.)

- [x] **Step 2: The user's check in `gbaview`**

Start it for the user in the background (Bash tool): `bash tools/dev.sh view "<path>"`. Note: `gbaview` keeps `<rom name>.sav` next to their ROM. Ask them to:
1. Press Enter at the title screen: the main menu must appear **without** "The internal battery has run dry".
2. Optional: continue or start a game, save in-game, close with Esc, reopen, and see the saved game continue (a real-world check of 3b's Flash 128K).

Record their answers in `docs/PROGRESS.md`. If the battery message appears, debug with superpowers:systematic-debugging (log the RTC command bytes the game sends with a temporary `fprintf` in `rtc.c`'s `begin`, compare with the facts at the top of this plan, and reproduce in `test_rtc.c` before fixing).

- [x] **Step 3: Full suite with sanitizers**

```bash
bash tools/dev.sh test 2>&1 | tail -3
bash tools/dev.sh unit 2>&1 | grep -E "runtime error|AddressSanitizer" || echo "sanitizers clean"
```

Expected: `100% tests passed, 0 tests failed out of 43` and `sanitizers clean`.

- [x] **Step 4: Update the docs and tag**

Update `docs/PROGRESS.md` (3b-2 done; next: 3c timing accuracy — prefetch buffer, open-bus and write-only register reads, mGBA timing/DMA suites — starting with brainstorming), set the 3b-2 spec's status to implemented, point `CLAUDE.md`'s "current" lines at "none yet — 3c needs brainstorming", then:

```bash
git add docs CLAUDE.md
git commit -m "docs: milestone 3b-2 complete - cartridge extras" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-3b2
```

- [x] **Step 5: Finish the branch**

Close any `gbaview` window first (it locks `gbaview.exe`). Use superpowers:finishing-a-development-branch (the user chooses; the repo has no remote). Delete the branch with `git branch -d milestone-3b2`.

---

## Self-review notes

- **Spec coverage:** §1 scope → header, Task 6; §2 files → file map; §3 game list, state, API, time keeping → Task 1 (+ reset re-basing tested in Task 3); §4 GPIO → Task 2; §5 RTC → Task 3; §6 solar, §7 gyro, §8 rumble → Task 2; §9 tilt → Task 1; §10 PC tools → Tasks 4–5; §11 testing → Tasks 1–3 unit tests, Task 6 real game; §12 done → Task 6.
- **Decisions made while planning:** the tilt readings start at the centre values (mGBA starts at 0xFFF; games take a reading first anyway); gyro samples are clamped even though the formula's range (0x340–0xA3F) never reaches the limits; `hardware_reset` runs before `gba_reset` zeroes the cycle count so the clock keeps running across resets, re-based to whole seconds; the RTC takes each written bit at the rising SCK edge (mGBA samples just before it — identical for game A's routine, which sets SIO while SCK is low); the solar result is the number of clocks until pin 3 answers (GBATEK's example loop reports one less).
- **Checked while writing:** the Unix times and weekdays at the top (Python `calendar.timegm`); the MSB-first command bytes (0x60/0x62/0x63/0x64/0x65/0x67) are exactly what game A's RTC code sends; mGBA's override list confirms every game code and save type in the table.
