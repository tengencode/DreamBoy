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
