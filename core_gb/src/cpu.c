/* The SM83 CPU: a plain interpreter. Time passes through the bus: every read, write and internal cycle calls
 * gb_tick, so each instruction takes exactly its machine cycles and the hardware is in step at every access.
 *
 * Opcodes are decoded by their bit fields: x = bits 7-6, y = bits 5-3, z = bits 2-0, p = y >> 1, q = y & 1. */
#include <string.h>

#include "gb_internal.h"

#define HL(c) ((uint16_t)((c)->h << 8 | (c)->l))

static uint8_t fetch(gb_t *g) {
    return gb_bus_read(g, g->cpu.pc++);
}

static uint16_t fetch16(gb_t *g) {
    uint8_t lo = fetch(g);
    return (uint16_t)(lo | fetch(g) << 8);
}

/* The 8-bit operand numbered i: B C D E H L (HL) A. */
static uint8_t get_r(gb_t *g, int i) {
    gb_cpu_t *c = &g->cpu;
    switch (i) {
    case 0: return c->b;
    case 1: return c->c;
    case 2: return c->d;
    case 3: return c->e;
    case 4: return c->h;
    case 5: return c->l;
    case 6: return gb_bus_read(g, HL(c));
    default: return c->a;
    }
}

static void set_r(gb_t *g, int i, uint8_t v) {
    gb_cpu_t *c = &g->cpu;
    switch (i) {
    case 0: c->b = v; break;
    case 1: c->c = v; break;
    case 2: c->d = v; break;
    case 3: c->e = v; break;
    case 4: c->h = v; break;
    case 5: c->l = v; break;
    case 6: gb_bus_write(g, HL(c), v); break;
    default: c->a = v; break;
    }
}

/* The 16-bit register numbered i: BC DE HL SP. */
static uint16_t get_rr(const gb_cpu_t *c, int i) {
    switch (i) {
    case 0: return (uint16_t)(c->b << 8 | c->c);
    case 1: return (uint16_t)(c->d << 8 | c->e);
    case 2: return HL(c);
    default: return c->sp;
    }
}

static void set_rr(gb_cpu_t *c, int i, uint16_t v) {
    switch (i) {
    case 0: c->b = (uint8_t)(v >> 8); c->c = (uint8_t)v; break;
    case 1: c->d = (uint8_t)(v >> 8); c->e = (uint8_t)v; break;
    case 2: c->h = (uint8_t)(v >> 8); c->l = (uint8_t)v; break;
    default: c->sp = v; break;
    }
}

static void push(gb_t *g, uint16_t v) {
    gb_bus_write(g, --g->cpu.sp, (uint8_t)(v >> 8));
    gb_bus_write(g, --g->cpu.sp, (uint8_t)v);
}

static uint16_t pop(gb_t *g) {
    uint8_t lo = gb_bus_read(g, g->cpu.sp++);
    return (uint16_t)(lo | gb_bus_read(g, g->cpu.sp++) << 8);
}

/* ADD ADC SUB SBC AND XOR OR CP on A. */
static void alu(gb_cpu_t *c, int op, uint8_t v) {
    int a = c->a, carry = (c->f & GB_FC) ? 1 : 0, r;
    switch (op) {
    case 0:
    case 1:
        carry = op == 1 ? carry : 0;
        r = a + v + carry;
        c->f = (uint8_t)(((r & 0xFF) ? 0 : GB_FZ) | ((a & 0xF) + (v & 0xF) + carry > 0xF ? GB_FH : 0) |
                         (r > 0xFF ? GB_FC : 0));
        c->a = (uint8_t)r;
        break;
    case 2:
    case 3:
    case 7:
        carry = op == 3 ? carry : 0;
        r = a - v - carry;
        c->f = (uint8_t)(((r & 0xFF) ? 0 : GB_FZ) | GB_FN | ((a & 0xF) - (v & 0xF) - carry < 0 ? GB_FH : 0) |
                         (r < 0 ? GB_FC : 0));
        if (op != 7) c->a = (uint8_t)r;
        break;
    case 4:
        c->a &= v;
        c->f = (uint8_t)((c->a ? 0 : GB_FZ) | GB_FH);
        break;
    case 5:
        c->a ^= v;
        c->f = c->a ? 0 : GB_FZ;
        break;
    default:
        c->a |= v;
        c->f = c->a ? 0 : GB_FZ;
        break;
    }
}

/* RLC RRC RL RR SLA SRA SWAP SRL. */
static uint8_t rotate(gb_cpu_t *c, int kind, uint8_t v) {
    int in = (c->f & GB_FC) ? 1 : 0, out = 0;
    switch (kind) {
    case 0: out = v >> 7; v = (uint8_t)(v << 1 | out); break;
    case 1: out = v & 1; v = (uint8_t)(v >> 1 | out << 7); break;
    case 2: out = v >> 7; v = (uint8_t)(v << 1 | in); break;
    case 3: out = v & 1; v = (uint8_t)(v >> 1 | in << 7); break;
    case 4: out = v >> 7; v = (uint8_t)(v << 1); break;
    case 5: out = v & 1; v = (uint8_t)(v >> 1 | (v & 0x80)); break;
    case 6: v = (uint8_t)(v << 4 | v >> 4); break;
    default: out = v & 1; v >>= 1; break;
    }
    c->f = (uint8_t)((v ? 0 : GB_FZ) | (out ? GB_FC : 0));
    return v;
}

static void prefix_cb(gb_t *g) {
    gb_cpu_t *c = &g->cpu;
    uint8_t op = fetch(g);
    int y = (op >> 3) & 7, z = op & 7;
    uint8_t v = get_r(g, z);
    switch (op >> 6) {
    case 0: set_r(g, z, rotate(c, y, v)); break;
    case 1: c->f = (uint8_t)((c->f & GB_FC) | GB_FH | ((v >> y) & 1 ? 0 : GB_FZ)); break;
    case 2: set_r(g, z, (uint8_t)(v & ~(1 << y))); break;
    default: set_r(g, z, (uint8_t)(v | 1 << y)); break;
    }
}

static void daa(gb_cpu_t *c) {
    int a = c->a, carry = (c->f & GB_FC) != 0;
    if (c->f & GB_FN) {
        if (carry) a -= 0x60;
        if (c->f & GB_FH) a -= 0x06;
    } else {
        if (carry || c->a > 0x99) {
            a += 0x60;
            carry = 1;
        }
        if ((c->f & GB_FH) || (c->a & 0x0F) > 0x09) a += 0x06;
    }
    c->a = (uint8_t)a;
    c->f = (uint8_t)((c->a ? 0 : GB_FZ) | (c->f & GB_FN) | (carry ? GB_FC : 0));
}

/* SP + a signed byte, with the flags ADD SP,e and LD HL,SP+e set. */
static uint16_t sp_plus(gb_cpu_t *c, uint8_t e) {
    c->f = (uint8_t)(((c->sp & 0xF) + (e & 0xF) > 0xF ? GB_FH : 0) | ((c->sp & 0xFF) + e > 0xFF ? GB_FC : 0));
    return (uint16_t)(c->sp + (int8_t)e);
}

/* The condition numbered i: NZ Z NC C. */
static bool cond(const gb_cpu_t *c, int i) {
    bool set = c->f & (i < 2 ? GB_FZ : GB_FC);
    return (i & 1) ? set : !set;
}

static void call(gb_t *g, uint16_t to) {
    gb_tick(g);
    push(g, g->cpu.pc);
    g->cpu.pc = to;
}

static void execute(gb_t *g, uint8_t op) {
    gb_cpu_t *c = &g->cpu;
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    uint16_t addr;
    uint8_t v;
    if (x == 1) {
        if (op == 0x76) { /* HALT; with interrupts off and one already waiting, the CPU does not stop and reads
                             the next byte twice instead */
            if (!c->ime && !c->ei_delay && (g->mem.ie & g->mem.iflag & 0x1F)) c->halt_bug = 1;
            else c->halted = 1;
            return;
        }
        if (op == 0x40) c->breakpoint = 1;
        set_r(g, y, get_r(g, z));
        return;
    }
    if (x == 2) {
        alu(c, y, get_r(g, z));
        return;
    }
    if (x == 0) {
        switch (z) {
        case 0:
            if (y == 0) return; /* NOP */
            if (y == 1) {       /* LD (nn),SP */
                addr = fetch16(g);
                gb_bus_write(g, addr, (uint8_t)c->sp);
                gb_bus_write(g, (uint16_t)(addr + 1), (uint8_t)(c->sp >> 8));
            } else if (y == 2) { /* STOP: skips a byte; switches a Game Boy Color's speed when that is armed (the
                                    divider starts again), else idles like HALT */
                c->pc++;
                if (gb_cgb_speed_switch(g)) gb_timer_write(g, 0xFF04, 0);
                else c->halted = 1;
            } else { /* JR e / JR cc,e */
                v = fetch(g);
                if (y == 3 || cond(c, y - 4)) {
                    gb_tick(g);
                    c->pc = (uint16_t)(c->pc + (int8_t)v);
                }
            }
            return;
        case 1:
            if (q == 0) {
                set_rr(c, p, fetch16(g));
            } else { /* ADD HL,rr */
                int hl = HL(c), rr = get_rr(c, p), r = hl + rr;
                c->f = (uint8_t)((c->f & GB_FZ) | ((hl & 0xFFF) + (rr & 0xFFF) > 0xFFF ? GB_FH : 0) |
                                 (r > 0xFFFF ? GB_FC : 0));
                set_rr(c, 2, (uint16_t)r);
                gb_tick(g);
            }
            return;
        case 2: /* A to or from (BC) (DE) (HL+) (HL-) */
            addr = get_rr(c, p < 2 ? p : 2);
            if (q == 0) gb_bus_write(g, addr, c->a);
            else c->a = gb_bus_read(g, addr);
            if (p >= 2) set_rr(c, 2, (uint16_t)(addr + (p == 2 ? 1 : -1)));
            return;
        case 3: /* INC rr / DEC rr */
            set_rr(c, p, (uint16_t)(get_rr(c, p) + (q ? -1 : 1)));
            gb_tick(g);
            return;
        case 4:
            v = (uint8_t)(get_r(g, y) + 1);
            c->f = (uint8_t)((c->f & GB_FC) | (v ? 0 : GB_FZ) | ((v & 0xF) == 0 ? GB_FH : 0));
            set_r(g, y, v);
            return;
        case 5:
            v = (uint8_t)(get_r(g, y) - 1);
            c->f = (uint8_t)((c->f & GB_FC) | GB_FN | (v ? 0 : GB_FZ) | ((v & 0xF) == 0xF ? GB_FH : 0));
            set_r(g, y, v);
            return;
        case 6:
            set_r(g, y, fetch(g));
            return;
        default:
            if (y < 4) { /* RLCA RRCA RLA RRA: as the CB ones, but Z is always clear */
                c->a = rotate(c, y, c->a);
                c->f &= GB_FC;
            } else if (y == 4) {
                daa(c);
            } else if (y == 5) {
                c->a = (uint8_t)~c->a;
                c->f |= GB_FN | GB_FH;
            } else if (y == 6) {
                c->f = (uint8_t)((c->f & GB_FZ) | GB_FC);
            } else {
                c->f = (uint8_t)((c->f & (GB_FZ | GB_FC)) ^ GB_FC);
            }
            return;
        }
    }
    switch (z) {
    case 0:
        if (y < 4) { /* RET cc */
            gb_tick(g);
            if (cond(c, y)) {
                c->pc = pop(g);
                gb_tick(g);
            }
        } else if (y == 4) {
            gb_bus_write(g, (uint16_t)(0xFF00 | fetch(g)), c->a);
        } else if (y == 6) {
            c->a = gb_bus_read(g, (uint16_t)(0xFF00 | fetch(g)));
        } else { /* ADD SP,e / LD HL,SP+e */
            addr = sp_plus(c, fetch(g));
            gb_tick(g);
            if (y == 5) {
                gb_tick(g);
                c->sp = addr;
            } else {
                set_rr(c, 2, addr);
            }
        }
        return;
    case 1:
        if (q == 0) { /* POP BC DE HL AF */
            addr = pop(g);
            if (p < 3) {
                set_rr(c, p, addr);
            } else {
                c->a = (uint8_t)(addr >> 8);
                c->f = (uint8_t)(addr & 0xF0);
            }
        } else if (p < 2) { /* RET / RETI */
            c->pc = pop(g);
            gb_tick(g);
            if (p == 1) c->ime = 1;
        } else if (p == 2) {
            c->pc = HL(c);
        } else {
            c->sp = HL(c);
            gb_tick(g);
        }
        return;
    case 2:
        if (y < 4) { /* JP cc,nn */
            addr = fetch16(g);
            if (cond(c, y)) {
                gb_tick(g);
                c->pc = addr;
            }
        } else {
            addr = (y & 1) ? fetch16(g) : (uint16_t)(0xFF00 | c->c);
            if (y < 6) gb_bus_write(g, addr, c->a);
            else c->a = gb_bus_read(g, addr);
        }
        return;
    case 3:
        if (y == 0) {
            addr = fetch16(g);
            gb_tick(g);
            c->pc = addr;
        } else if (y == 1) {
            prefix_cb(g);
        } else if (y == 6) {
            c->ime = c->ei_delay = 0;
        } else if (y == 7) {
            if (!c->ime && !c->ei_delay) c->ei_delay = 2;
        } else {
            c->locked = 1;
        }
        return;
    case 4:
        if (y >= 4) {
            c->locked = 1;
            return;
        }
        addr = fetch16(g);
        if (cond(c, y)) call(g, addr);
        return;
    case 5:
        if (q == 0) { /* PUSH BC DE HL AF */
            gb_tick(g);
            push(g, p < 3 ? get_rr(c, p) : (uint16_t)(c->a << 8 | c->f));
        } else if (p == 0) {
            addr = fetch16(g);
            call(g, addr);
        } else {
            c->locked = 1;
        }
        return;
    case 6:
        alu(c, y, fetch(g));
        return;
    default:
        call(g, (uint16_t)(y * 8));
        return;
    }
}

/* Enter the interrupt with the lowest number that is both requested and enabled. The choice is made after the
 * high byte of PC is pushed: if that write changed IE so nothing is left, the CPU jumps to 0x0000. */
static void interrupt(gb_t *g) {
    gb_cpu_t *c = &g->cpu;
    c->ime = 0;
    c->halted = 0;
    gb_tick(g);
    gb_tick(g);
    gb_bus_write(g, --c->sp, (uint8_t)(c->pc >> 8));
    uint8_t pending = g->mem.ie & g->mem.iflag & 0x1F;
    gb_bus_write(g, --c->sp, (uint8_t)c->pc);
    c->pc = 0;
    for (int bit = 0; bit < 5; bit++) {
        if (pending >> bit & 1) {
            g->mem.iflag &= (uint8_t)~(1 << bit);
            c->pc = (uint16_t)(0x40 + bit * 8);
            break;
        }
    }
    gb_tick(g);
}

void gb_cpu_step(gb_t *g) {
    gb_cpu_t *c = &g->cpu;
    if (c->locked) {
        gb_tick(g);
        return;
    }
    for (; g->cgb.stall; g->cgb.stall--) gb_tick(g); /* a copy into video RAM holds the CPU */
    if (c->ei_delay && --c->ei_delay == 0) c->ime = 1;
    if (c->ime && (g->mem.ie & g->mem.iflag & 0x1F)) {
        interrupt(g);
        return;
    }
    if (c->halted) {
        gb_tick(g);
        if (g->mem.ie & g->mem.iflag & 0x1F) c->halted = 0;
        return;
    }
    uint8_t op = gb_bus_read(g, c->pc);
    if (c->halt_bug) c->halt_bug = 0;
    else c->pc++;
    execute(g, op);
}

/* The registers as the boot ROM leaves them (the original Game Boy, a Game Boy Color). */
void gb_cpu_reset(gb_t *g) {
    gb_cpu_t *c = &g->cpu;
    memset(c, 0, sizeof *c);
    if (g->info.color) {
        c->a = 0x11, c->f = 0x80, c->d = 0xFF, c->e = 0x56, c->l = 0x0D;
    } else {
        c->a = 0x01, c->f = 0xB0, c->c = 0x13, c->e = 0xD8, c->h = 0x01, c->l = 0x4D;
    }
    c->sp = 0xFFFE;
    c->pc = 0x0100;
}
