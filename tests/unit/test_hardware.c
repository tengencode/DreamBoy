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

/* A tilt game's sampling routine: read X/Y (waiting for ready), then start the next conversion. */
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
