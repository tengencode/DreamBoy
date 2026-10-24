# Milestone 3b-2 — Cartridge Extras (RTC, Solar, Gyro, Rumble, Tilt, Game List) — Design

**Date:** 2026-09-25
**Status:** Implemented — milestone 3b-2 complete 2026-09-25 (tag `milestone-3b2`)
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§2.2 public API, §4.5 cartridge, §10 milestones)
**Builds on:** milestone 3b (tag `milestone-3b`): save chips and save-type detection.

---

## 1. Goal and scope

Emulate the extra hardware some cartridges carry — the real-time clock, the solar sensor, the gyro, rumble and the tilt sensor — and know which games have which, so games like game A run without "The internal battery has run dry".

**User decisions (2026-09-25):** all extras in one part; testing by unit tests that replay each game's documented access routine step by step (no free test ROMs exist); a final real-game check with the user's own game A ROM (kept on their PC, never copied into the project).

| In scope | Out of scope |
|---|---|
| GPIO port (0x080000C4–C9) | RTC interrupts and alarms, clock adjustment register |
| RTC S-3511: reset, date+time, time, control, force-IRQ (ignored) | Writing a new date/time into the RTC (accepted and ignored, as mGBA does) |
| Solar sensor, gyro, rumble (GPIO); tilt sensor (0x0E008xxx) | Game Boy Player rumble, e-Reader, link devices |
| Game list keyed by game code (+ `SIIRTC_V` fallback) incl. save types | CRC32-keyed entries (codes are enough for these games) |
| Public API setters/getters; test runner and `gbaview` inputs | Android sensor wiring (milestone 6) |

---

## 2. Files

```
core/include/gba.h            + GBA_HW_* flags, gba_cart_hardware(), gba_set_cart_hardware(),
                                gba_set_rtc(), gba_set_light(), gba_set_gyro(), gba_set_tilt(), gba_rumble_active()
core/src/cart/hardware.h / hardware.c   game list, hardware state, API glue, tilt sensor
core/src/cart/gpio.c          GPIO port; solar sensor, gyro, rumble on its pins
core/src/cart/rtc.c           S-3511 clock: serial protocol, calendar from Unix time
core/src/cart/cart.h / cart.c + hardware_t member; hardware_setup() at load
core/src/cart/save.c          0x0E008000-0x0E0085FF go to the tilt sensor when present
core/src/mem/bus.c            reads of ROM offsets 0xC4-0xC9 through the GPIO port; 16/32-bit writes there
tools/testrunner/main.c       + --rtc <unix seconds>, --hardware <list>
tools/viewer/main.c           + PC local time, keys for tilt/gyro/light, rumble and light level in the title
tests/unit/test_rtc.c, test_gpio.c, test_hardware.c
```

---

## 3. Game list and hardware state

- `hardware_t` (inside `cart_t`): device flags, GPIO registers and pin state, RTC state, solar/gyro/tilt state and the current sensor inputs; the clock base (Unix seconds + UTC offset, and the emulated cycle at which it was set).
- **Game list** (first three letters of the game code, so every region matches; from mGBA's override list):

| Code | Game | Extras | Save |
|---|---|---|---|
| AXV, AXP, BPE | game A, game A, game A | RTC | Flash 128K |
| U3I, U32, U33 | game E 1, 2, 3 | RTC + solar | EEPROM |
| BR4 | game J | RTC | Flash 64K |
| BKA | game I | RTC | Flash 128K |
| BLJ, BLV | game K (1, 2) | RTC | Flash 64K |
| RZW | game D | gyro + rumble | SRAM |
| V49 | game H | rumble | SRAM |
| 2GB | (listed by mGBA with rumble; title not confirmed) | rumble | SRAM |
| KYG | game F | tilt | EEPROM |
| KHP | game G | tilt | EEPROM |

- A listed game gets its extras and its save type (the save type replaces what marker detection chose; EEPROM keeps size detection). An unlisted ROM containing `SIIRTC_V` (the RTC library's name, 4-byte aligned) gets the RTC.
- API (`core/include/gba.h`):

```c
enum { GBA_HW_RTC = 1 << 0, GBA_HW_SOLAR = 1 << 1, GBA_HW_GYRO = 1 << 2, GBA_HW_RUMBLE = 1 << 3, GBA_HW_TILT = 1 << 4 };
uint32_t gba_cart_hardware(const gba_t *g);            /* GBA_HW_* flags of the loaded cartridge */
void gba_set_cart_hardware(gba_t *g, uint32_t flags);  /* manual override (resets the extras' state) */
void gba_set_rtc(gba_t *g, int64_t unix_seconds, int32_t utc_offset_s); /* the clock reads this time now */
void gba_set_light(gba_t *g, uint8_t level);           /* 0 = dark .. 255 = full sun */
void gba_set_gyro(gba_t *g, int16_t z_rate);           /* rotation speed: > 0 clockwise; ±32767 = very fast */
void gba_set_tilt(gba_t *g, int16_t x, int16_t y);     /* ±32767 = fully tilted; 0 = level */
int gba_rumble_active(const gba_t *g);                 /* 1 while the rumble motor is on */
```

- **Time keeping:** the clock shows `unix_seconds + utc_offset_s + (cycles since gba_set_rtc) / 16,777,216`. Without any `gba_set_rtc` call it starts at 2000-01-01 00:00:00 when the ROM is loaded. `gba_reset` keeps the time; sensor inputs are kept too; the extras' protocol state is cleared.

## 4. GPIO port

- ROM offsets 0xC4 (data, 4 pins), 0xC6 (direction: 1 = output from the GBA), 0xC8 (bit 0: 1 = registers readable), in every ROM mirror (0x08–0x0D). Only when the cartridge has an RTC, solar sensor, gyro or rumble.
- **Writes** (16- and 32-bit; 8-bit writes are ignored): data stores the written bits for output pins; direction stores the directions; control stores readability. After each write the devices react to the new pin levels.
- **Reads:** with control bit 0 set, 0xC4 returns the pin levels (outputs from the GBA's written values, inputs from the devices), 0xC6 the directions, 0xC8 the control bit; otherwise the ROM's own bytes (normally zero).
- Pin use: RTC — 0 SCK, 1 SIO, 2 CS; solar — 0 clock, 1 reset, 3 result; gyro — 0 start, 1 clock, 2 data; rumble — 3 motor.

## 5. Real-time clock (S-3511)

- **Transfer:** CS high starts a transfer; CS low ends it and resets the transfer state. While CS is low, SIO reads back 1.
- **Bits:** the GBA puts a bit on SIO and raises SCK: the bit is taken on the rising edge, least significant bit first. When reading, the RTC puts the next bit on SIO when SCK falls.
- **Command byte:** bits 0–3 must be `0110` (6), bits 4–6 the command, bit 7 set = read. (game A sends 0x65 most-significant-first, which arrives as 0xA6: command 2, read.) Any other value is ignored.
- **Commands:** 0 reset (control ← 0x00); 2 date+time (7 bytes); 3 force IRQ (ignored); 4 control (1 byte); 6 time (3 bytes). Reads of 1, 5, 7 return 0xFF.
- **Date+time bytes** (BCD): year (00–99 = 2000–2099), month 1–12, day 1–31, weekday 0–6 (0 = Sunday), hour (0–23 in 24-hour mode, 0–11 in 12-hour mode), minute, second. The time is latched when a date+time or time command starts. Longer reads repeat the parameter block.
- **Control:** bit 6 = 24-hour mode; power-on value 0x40 (no "power lost" flag, so games see a healthy clock). Writes of date/time are accepted and ignored.
- **Calendar:** converted from Unix seconds with a pure integer days-to-civil routine (no C library time functions, so results do not depend on the PC).

## 6. Solar sensor (game E)

- Pin 1 high resets the counter to 0 and samples the light level. Each rising edge on pin 0 (after a low) adds 1. Pin 3 reads 1 once the counter has reached the target.
- Target = `0xE8 − level × (0xE8 − 0x50) / 255`: 0xE8 in the dark (GBATEK's darkness value), 0x50 at level 255 (game E's full gauge).
- The sensor does nothing while pin 2 (the RTC's CS) is high.

## 7. Gyro (game D)

- Pin 0 high takes a sample: `value = clamp(0x6C0 + z_rate × 0x380 / 32768, 0, 0xFFF)`, held in a 16-bit shift register as 4 zero bits followed by the 12 bits.
- Each falling edge of pin 1 moves the next bit onto pin 2 (the first bit is available right after the sample).

## 8. Rumble

- On while pin 3 is an output and high; `gba_rumble_active` reports it.

## 9. Tilt sensor (game F, game G)

- Writes: 0x55 to 0x0E008000, then 0xAA to 0x0E008100 → a reading is taken: `x = 0x392 + tilt_x × 0xE0 / 32768`, `y = 0x3A0 + tilt_y × 0xE0 / 32768` (12 bits).
- Reads: 0x0E008200 x low byte, 0x0E008300 x bits 8–11 plus bit 7 = ready (always 1), 0x0E008400 y low byte, 0x0E008500 y bits 8–11. Other offsets in 0x0E008000–0x0E0085FF read 0xFF. These addresses only belong to the tilt sensor when the cartridge has one (its saves are EEPROM, so the area is otherwise unused).

## 10. PC tools

- **Test runner:** `--rtc <unix seconds>` calls `gba_set_rtc(g, seconds, 0)` after loading; `--hardware <list>` (comma-separated `rtc,solar,gyro,rumble,tilt`, or `none`) calls `gba_set_cart_hardware`.
- **gbaview:** at start, `gba_set_rtc(time(NULL), local UTC offset)`. Keys: I/J/K/L tilt (held = ±24000), Q/E gyro (held = ∓/±16000), `[` / `]` light −/+ 32 (starts at 0). The window title shows `light <n>` when the cartridge has a solar sensor and `RUMBLE` while the motor runs.

## 11. Testing

1. **Unit tests replaying game routines** (from GBATEK's example code and game A's RTC access pattern): RTC date+time read in 24-hour and 12-hour modes, time read, control write/read, reset, bad command ignored, CS low aborting mid-transfer, the clock moving with emulated seconds, calendar edge cases (midnight, month end, 29 February 2024, year 2099); solar loop result 0xE8 at level 0 and 0x50 at 255; gyro 16-bit read = 0x6C0 still, higher clockwise, lower anticlockwise, clamped; rumble on/off with pin 3 and direction; tilt sampling routine at centre and extremes with the ready bit; GPIO register readability and ignored byte writes; game list for each code prefix and region letter, save-type override, `SIIRTC_V` fallback, manual override, no extras for unknown games.
2. **Real game (user):** their game A ROM in `gbaview` reaches the main menu without "The internal battery has run dry"; saving in-game, closing and reopening continues the game (also checks milestone 3b's Flash 128K in a real game).
3. All earlier tests keep passing; ASan + UBSan on.

## 12. Done when

- All unit tests pass (earlier milestones included).
- The user's game A check passes.
- Sanitizers report nothing.
