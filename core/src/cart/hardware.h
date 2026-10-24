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

/* gpio.c: ROM offsets 0xC4-0xC9 in regions 0x08-0x0D. */
bool gpio_mapped(const gba_t *g, uint32_t addr);
uint32_t gpio_read(gba_t *g, uint32_t addr, unsigned size);
void gpio_write(gba_t *g, uint32_t addr, uint32_t value, unsigned size);

/* rtc.c */
void rtc_calendar(int64_t local_seconds, bool hour24, uint8_t out[7]); /* BCD date+time */
uint8_t rtc_pins(rtc_t *r, uint8_t out, int64_t local_seconds);       /* returns the SIO level << 1 */

#endif
