#ifndef GB_INTERNAL_H
#define GB_INTERNAL_H

#include <stdbool.h>

#include "gb.h"
#include "gb_debug.h"

/* Every name seen outside its file starts with gb_: both cores are linked into one program. */

#define GB_AUDIO_FRAMES 2048 /* stereo samples kept for gb_audio_read: about 3.7 frames */

typedef enum { MBC_NONE, MBC1, MBC2, MBC3, MBC5 } mbc_t;

/* What the loaded ROM is: set by gb_load_rom, never part of a save state. */
typedef struct {
    uint8_t *rom;      /* a power-of-two copy, padded with 0xFF */
    uint32_t rom_size; /* of the copy */
    uint32_t crc32;    /* of the bytes as given */
    char title[17];
    bool color;
    mbc_t mbc;
    bool battery, clock, rumble;
    uint32_t ram_size; /* 0 or a power of two */
} cart_info_t;

/* CPU flags (register F). */
#define GB_FZ 0x80
#define GB_FN 0x40
#define GB_FH 0x20
#define GB_FC 0x10

typedef struct {
    uint8_t a, f, b, c, d, e, h, l;
    uint16_t sp, pc;
    uint8_t ime;        /* interrupts on */
    uint8_t ei_delay;   /* EI: ime turns on when this counts down to 0 (after the next instruction) */
    uint8_t halted;     /* HALT / STOP: idle until an interrupt is pending */
    uint8_t halt_bug;   /* the next opcode byte is read without moving PC */
    uint8_t locked;     /* an illegal opcode: the CPU has stopped for good */
    uint8_t breakpoint; /* LD B,B ran */
} gb_cpu_t;

typedef struct {
    uint8_t wram[GB_WRAM_SIZE];
    uint8_t hram[0x7F];
    uint8_t iflag, ie; /* interrupt requests (5 bits) and enables */
    uint8_t wram_bank; /* the bank at 0xD000: 1-7 */
} gb_mem_t;

typedef struct {
    uint16_t div;      /* clock ticks, wrapping; DIV is the high byte */
    uint8_t tima, tma, tac;
    uint8_t overflow;  /* TIMA wrapped in the last cycle: TMA is loaded and the interrupt asked for in the next */
    uint8_t reloading; /* this is that next cycle */
} gb_timer_t;

typedef struct {
    uint8_t sb, sc;
    uint8_t bits_left; /* of a running transfer with the internal clock */
} gb_serial_t;

typedef struct {
    uint8_t select; /* P1 bits 4-5 as written */
    uint8_t keys;   /* GB_KEY_* held */
} gb_joypad_t;

typedef struct {
    uint8_t lcdc, stat; /* stat: the four interrupt-enable bits as written */
    uint8_t scy, scx, ly, lyc, dma, bgp, obp0, obp1, wy, wx;
    uint8_t mode;        /* 0 HBlank, 1 VBlank, 2 OAM scan, 3 drawing: as STAT shows it */
    uint8_t irq_mode;    /* the same as the interrupt sources see it: a little ahead of what STAT shows */
    uint8_t oam_busy, vram_busy; /* the CPU cannot reach them */
    uint8_t coincidence; /* the LY = LYC flag */
    uint8_t stat_line;   /* the STAT interrupt line's level */
    uint8_t phase;       /* what the line is waiting for (ppu.c) */
    uint8_t line;        /* 0-153; LY reads this, except in line 153 */
    uint8_t window_line; /* the window's own line counter */
    uint8_t window_hit;  /* LY reached WY in this frame */
    uint8_t dma_active, dma_delay, dma_index, dma_blocking; /* OAM DMA */
    uint16_t dot;        /* 0-455 in the line */
    uint16_t next;       /* the dot at which the phase ends */
    uint8_t vram[0x4000]; /* two banks (a plain Game Boy uses the first) */
    uint8_t oam[0xA0];
} gb_ppu_t;

typedef struct {
    uint8_t on;        /* playing (its bit in NR52) */
    uint8_t dac;       /* its converter is on: it is heard at all */
    uint8_t length_on; /* the length counter stops it at 0 */
    uint8_t volume, env_timer;
    uint8_t pos;       /* step in the duty pattern (0-7) or the wave (0-31) */
    uint16_t length;
    uint16_t freq;     /* 11 bits */
    int32_t timer;     /* ticks until the next step */
} gb_chan_t;

typedef struct {
    gb_chan_t ch[4];   /* square with sweep, square, wave, noise */
    uint8_t reg[0x17]; /* 0xFF10-0xFF26 as written */
    uint8_t wave[16];
    uint8_t power;
    uint8_t seq_step;  /* the sequencer step that comes next, 0-7 */
    uint8_t sweep_timer, sweep_on, sweep_negged;
    uint8_t wave_sample;    /* the 4-bit sample the wave channel is playing */
    uint8_t wave_just_read; /* it read wave RAM in this cycle */
    uint8_t sum_dots;       /* ticks gathered towards the next output sample */
    uint16_t sweep_freq;
    uint16_t lfsr;          /* the noise channel's shift register */
    int32_t sum_left, sum_right;
    int32_t cap_left, cap_right; /* the output filter */
} gb_apu_t;

/* Game Boy Color only. */
typedef struct {
    uint8_t double_speed, speed_armed;
    uint8_t vbk;          /* the video RAM bank, 0-1 */
    uint8_t svbk;         /* the work RAM bank register as written (mem.wram_bank is what it selects) */
    uint8_t bcps, ocps;   /* palette indexes (bit 7: move on after each write) */
    uint8_t opri;         /* 1: sprites overlap by the plain Game Boy's rule */
    uint8_t rp, ff72, ff73, ff74, ff75;
    uint8_t hdma_active;  /* a per-line copy is running */
    uint8_t hdma_left;    /* its blocks still to go, less 1 */
    uint16_t hdma_src, hdma_dst;
    uint16_t stall;       /* machine cycles the CPU still stands still for a copy */
    uint8_t bg_palette[64], obj_palette[64];
} gb_cgb_t;

/* The banking chip's registers. */
typedef struct {
    uint8_t ram_on;
    uint8_t mode;     /* MBC1: 1 = the high bits also move the 0x0000 area and pick the RAM bank */
    uint8_t bank_lo;  /* the ROM bank register (MBC5: its low 8 bits) */
    uint8_t bank_hi;  /* MBC1: the 2 high bits; MBC5: bit 8 */
    uint8_t ram_bank; /* as written (MBC3: 8-12 pick a clock register) */
    uint8_t dirty;    /* cartridge RAM was written */
    uint32_t rom0, rom1; /* where the banks at 0x0000 and 0x4000 start in the ROM */
    uint32_t ram_at;     /* where the bank at 0xA000 starts in the RAM */
} gb_cart_t;

struct gb {
    cart_info_t info;
    uint8_t *cart_ram; /* info.ram_size bytes */
    gb_cpu_t cpu;
    gb_mem_t mem;
    gb_timer_t timer;
    gb_serial_t serial;
    gb_joypad_t joypad;
    gb_cart_t cart;
    gb_ppu_t ppu;
    gb_apu_t apu;
    gb_cgb_t cgb;
    int16_t audio[GB_AUDIO_FRAMES * 2]; /* samples not yet read: a ring */
    uint32_t audio_read, audio_count;
    uint16_t back[GB_WIDTH * GB_HEIGHT]; /* the picture being drawn; shown (copied to frame) when VBlank starts */
    uint16_t frame[GB_WIDTH * GB_HEIGHT]; /* the picture, BGR555 */
    char serial_out[4096]; /* for tests: every byte sent on the serial port */
    uint32_t serial_len;
    uint64_t ticks;     /* clock ticks since reset */
    int32_t frame_left; /* ticks left in the frame gb_run_frame is running (<= 0: done; the overshoot carries over) */
};

/* One machine cycle passes: everything but the CPU moves on. Called before each bus access and for each of the
 * CPU's internal cycles. */
void gb_tick(gb_t *g);
uint8_t gb_bus_read(gb_t *g, uint16_t addr); /* a CPU read: one cycle, then the value */
void gb_bus_write(gb_t *g, uint16_t addr, uint8_t value);
uint8_t gb_bus_peek(const gb_t *g, uint16_t addr); /* the value, no time passing */
void gb_cpu_reset(gb_t *g);
void gb_cpu_step(gb_t *g); /* one instruction, interrupt entry or idle cycle */

void gb_timer_tick(gb_t *g);
uint8_t gb_timer_read(const gb_t *g, uint16_t addr); /* 0xFF04-0xFF07 */
void gb_timer_write(gb_t *g, uint16_t addr, uint8_t value);
void gb_serial_clock(gb_t *g); /* a falling edge of the serial clock */
uint8_t gb_serial_read(const gb_t *g, uint16_t addr); /* 0xFF01-0xFF02 */
void gb_serial_write(gb_t *g, uint16_t addr, uint8_t value);
uint8_t gb_joypad_read(const gb_t *g);
void gb_joypad_write(gb_t *g, uint8_t value);

void gb_ppu_reset(gb_t *g);
void gb_ppu_tick(gb_t *g, int dots);
void gb_dma_tick(gb_t *g); /* once a machine cycle */
uint8_t gb_ppu_read(const gb_t *g, uint16_t addr); /* 0xFF40-0xFF4B */
void gb_ppu_write(gb_t *g, uint16_t addr, uint8_t value);
bool gb_ppu_vram_blocked(const gb_t *g); /* the CPU cannot reach video RAM while a line is drawn */
bool gb_ppu_oam_blocked(const gb_t *g);  /* nor OAM during the scan, the drawing, or OAM DMA */
int gb_ppu_line_sprites(const gb_t *g, int ly, uint8_t out[10]);
void gb_ppu_draw_line(gb_t *g, int ly);
void gb_apu_reset(gb_t *g);
void gb_apu_tick(gb_t *g, int dots);
void gb_apu_sequencer(gb_t *g); /* a 512 Hz step: the divider's bit 12 fell */
uint8_t gb_apu_read(const gb_t *g, uint16_t addr); /* 0xFF10-0xFF3F */
void gb_apu_write(gb_t *g, uint16_t addr, uint8_t value);
void gb_cgb_reset(gb_t *g);
uint8_t gb_cgb_read(const gb_t *g, uint16_t addr); /* 0xFF4D-0xFF75, the Game Boy Color's own registers */
void gb_cgb_write(gb_t *g, uint16_t addr, uint8_t value);
void gb_cgb_hblank(gb_t *g);
bool gb_cgb_speed_switch(gb_t *g); /* STOP: true when it switched the speed */
void gb_cart_reset(gb_t *g);
uint8_t gb_cart_read(const gb_t *g, uint16_t addr); /* 0x0000-0x7FFF and 0xA000-0xBFFF */
void gb_cart_write(gb_t *g, uint16_t addr, uint8_t value);
gb_err_t gb_cart_load(gb_t *g, const uint8_t *rom, size_t size);
void gb_cart_free(gb_t *g);

#endif
