#include <stdlib.h>
#include <string.h>

#include "bios/hle.h"
#include "cheats/cheats.h"
#include "gba_internal.h"
#include "rewind.h"
#include "sys/keypad.h"

_Static_assert(GBA_IWRAM_SIZE == IWRAM_SIZE && GBA_EWRAM_SIZE == EWRAM_SIZE, "gba.h's RAM sizes match the bus");

gba_t *gba_create(const gba_config_t *cfg) {
    (void)cfg;
    cpu_init_tables();
    io_init_tables();
    gba_t *g = calloc(1, sizeof(gba_t));
    if (!g) return NULL;
    g->hle_bios = true;
    gba_reset(g);
    return g;
}

void gba_destroy(gba_t *g) {
    if (!g) return;
    cheats_drop(g);
    cart_unload(&g->cart);
    rewind_free(g->rewind);
    free(g);
}

gba_err_t gba_load_bios(gba_t *g, const uint8_t *bios, size_t size) {
    if (size != BIOS_SIZE) return GBA_ERR_BIOS_SIZE;
    memcpy(g->bus.bios, bios, BIOS_SIZE);
    g->hle_bios = false;
    return GBA_OK;
}

gba_err_t gba_load_rom(gba_t *g, const uint8_t *rom, size_t size) {
    cheats_drop(g); /* their ROM patches and addresses belong to the old ROM */
    gba_err_t err = cart_load(&g->cart, rom, size);
    if (err == GBA_OK) {
        hardware_setup(g);
        gba_reset(g);
    }
    return err;
}

void gba_reset(gba_t *g) {
    rewind_clear(g->rewind);
    hardware_reset(g); /* before the cycle count restarts: it re-bases the clock */
    g->now = 0;
    g->frame_done = false;
    g->keyinput = 0x3FF;
    sched_init(&g->sched);
    bus_reset(g);
    io_reset(g);
    irq_reset(g);
    save_reset(&g->cart.save);
    timer_reset(g);
    dma_reset(g);
    sio_reset(g);
    apu_reset(g);
    ppu_reset(g);
    cpu_reset(g);
    if (g->hle_bios) hle_boot(g);
    NOTES_CLEAR(g); /* the built-in BIOS's own start-up is not the game's */
}

void gba_run_events(gba_t *g) {
    uint64_t when;
    int id;
    while ((id = sched_pop_due(&g->sched, g->now, &when)) >= 0) {
        switch (id) {
        case EV_HBLANK: ppu_on_hblank(g, when); break;
        case EV_LINE_END: ppu_on_line_end(g, when); break;
        case EV_TIMER0:
        case EV_TIMER1:
        case EV_TIMER2:
        case EV_TIMER3: timer_on_overflow(g, id - EV_TIMER0, when); break;
        case EV_DMA: dma_run_immediate(g); break;
        case EV_APU_FRAME: apu_on_frame_seq(g, when); break;
        case EV_APU_SAMPLE: apu_on_sample(g, when); break;
        case EV_SIO: sio_on_transfer_done(g); break;
        default: break;
        }
    }
}

static void run_frame(gba_t *g) {
    g->frame_done = false;
    while (!g->frame_done) {
        if (g->irq.stopped) {
            if (!irq_stop_wake(g)) {
                apu_stopped_frame(g); /* nothing inside moves; the frontend still gets its sound */
                return;
            }
            g->irq.stopped = false;
        }
        /* Re-read the next event after every instruction: an I/O write can schedule one sooner
         * (an immediate DMA must run before the CPU continues). */
        while (g->now < sched_next(&g->sched) && !g->irq.stopped) {
            if (g->irq.halted) {
                if (!irq_wake(g)) {
                    uint64_t next = sched_next(&g->sched);
                    if (g->irq.pending && irq_wake_at(g) < next) {
                        g->now = irq_wake_at(g); /* the request reaches the halted CPU: wake then */
                        continue;
                    }
                    g->now = next; /* nothing can happen before the next event */
                    break;
                }
                g->irq.halted = false;
            }
            cpu_run(g);
        }
        gba_run_events(g);
    }
}

void gba_run_frame(gba_t *g) {
    run_frame(g);
    NOTES_FRAME(g);
    cheats_apply(g); /* VBlank has just started: before the next frame's code, before the snapshot */
    rewind_after_frame(g);
}

void gba_set_keys(gba_t *g, uint16_t pressed) {
    g->keyinput = (uint16_t)(~pressed & 0x3FF);
    keypad_check(g);
}

const uint16_t *gba_framebuffer(const gba_t *g) {
    return g->ppu.framebuffer;
}

size_t gba_audio_read(gba_t *g, int16_t *out, size_t max_frames) {
    return apu_read(g, out, max_frames);
}

const char *gba_rom_title(const gba_t *g) {
    return g->cart.title;
}

uint32_t gba_rom_crc32(const gba_t *g) {
    return g->cart.crc32;
}

const uint8_t *gba_iwram(const gba_t *g) {
    return g->bus.iwram;
}

const uint8_t *gba_ewram(const gba_t *g) {
    return g->bus.ewram;
}

const char *gba_rom_code(const gba_t *g) {
    return g->cart.code;
}
