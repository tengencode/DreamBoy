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
    if (h->devices & GBA_HW_RTC) driven |= rtc_pins(&h->rtc, out, hardware_local_seconds(g));
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
