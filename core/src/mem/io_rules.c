#include "mem/io.h"

#include <stddef.h>

/* Readable I/O halfwords and their readable bits, and the unused slots that read 0. Everything not
 * listed reads open bus (write-only registers and holes). Sources: GBATEK's I/O map, mGBA's register
 * tables and the expectations of the mGBA suite's "I/O read" and "SIO register R/W" groups. */
typedef struct {
    uint16_t first, last;
    uint8_t kind;
    uint16_t mask;
} io_range_t;

#define READ(first, last, mask) {first, last, IO_READ, mask}
#define ZERO(first, last) {first, last, IO_ZERO, 0}

static const io_range_t RANGES[] = {
    READ(0x000, 0x000, 0xFFF7), /* DISPCNT (bit 3: CGB mode, only the BIOS sets it) */
    READ(0x002, 0x002, 0x0001), /* green swap */
    READ(0x004, 0x004, 0xFF3F), /* DISPSTAT */
    READ(0x006, 0x006, 0x00FF), /* VCOUNT */
    READ(0x008, 0x00A, 0xDFFF), /* BG0CNT, BG1CNT: bit 13 (wrap) only exists for BG2/BG3 */
    READ(0x00C, 0x00E, 0xFFFF), /* BG2CNT, BG3CNT */
    READ(0x048, 0x04A, 0x3F3F), /* WININ, WINOUT */
    READ(0x050, 0x050, 0x3FFF), /* BLDCNT */
    READ(0x052, 0x052, 0x1F1F), /* BLDALPHA */
    READ(0x060, 0x060, 0x007F), /* SOUND1CNT_L */
    READ(0x062, 0x062, 0xFFC0), /* SOUND1CNT_H: the length is write-only */
    READ(0x064, 0x064, 0x4000), /* SOUND1CNT_X: only the length flag */
    ZERO(0x066, 0x066),
    READ(0x068, 0x068, 0xFFC0), /* SOUND2CNT_L */
    ZERO(0x06A, 0x06A),
    READ(0x06C, 0x06C, 0x4000), /* SOUND2CNT_H */
    ZERO(0x06E, 0x06E),
    READ(0x070, 0x070, 0x00E0), /* SOUND3CNT_L */
    READ(0x072, 0x072, 0xE000), /* SOUND3CNT_H */
    READ(0x074, 0x074, 0x4000), /* SOUND3CNT_X */
    ZERO(0x076, 0x076),
    READ(0x078, 0x078, 0xFF00), /* SOUND4CNT_L */
    ZERO(0x07A, 0x07A),
    READ(0x07C, 0x07C, 0x40FF), /* SOUND4CNT_H */
    ZERO(0x07E, 0x07E),
    READ(0x080, 0x080, 0xFF77), /* SOUNDCNT_L */
    READ(0x082, 0x082, 0x770F), /* SOUNDCNT_H */
    READ(0x084, 0x084, 0x008F), /* SOUNDCNT_X: master enable + channel status */
    ZERO(0x086, 0x086),
    READ(0x088, 0x088, 0xC3FE), /* SOUNDBIAS */
    ZERO(0x08A, 0x08A),
    READ(0x090, 0x09E, 0xFFFF), /* wave RAM */
    ZERO(0x0B8, 0x0B8),         /* DMA0CNT_L */
    READ(0x0BA, 0x0BA, 0xF7E0), /* DMA0CNT_H (bit 11, game pak DRQ, is DMA3 only) */
    ZERO(0x0C4, 0x0C4),
    READ(0x0C6, 0x0C6, 0xF7E0),
    ZERO(0x0D0, 0x0D0),
    READ(0x0D2, 0x0D2, 0xF7E0),
    ZERO(0x0DC, 0x0DC),
    READ(0x0DE, 0x0DE, 0xFFE0), /* DMA3CNT_H */
    READ(0x100, 0x100, 0xFFFF), /* TM0CNT_L: the counter */
    READ(0x102, 0x102, 0x00C7), /* TM0CNT_H */
    READ(0x104, 0x104, 0xFFFF),
    READ(0x106, 0x106, 0x00C7),
    READ(0x108, 0x108, 0xFFFF),
    READ(0x10A, 0x10A, 0x00C7),
    READ(0x10C, 0x10C, 0xFFFF),
    READ(0x10E, 0x10E, 0x00C7),
    READ(0x120, 0x12A, 0xFFFF), /* link port data and SIOCNT (the link port applies its own rules) */
    READ(0x130, 0x130, 0x03FF), /* KEYINPUT */
    READ(0x132, 0x132, 0xC3FF), /* KEYCNT */
    READ(0x134, 0x134, 0xFFFF), /* RCNT */
    ZERO(0x136, 0x136),
    READ(0x140, 0x140, 0xFFFF), /* JOYCNT */
    ZERO(0x142, 0x142),
    READ(0x150, 0x158, 0xFFFF), /* JOY_RECV, JOY_TRANS, JOYSTAT */
    ZERO(0x15A, 0x15A),
    READ(0x200, 0x202, 0x3FFF), /* IE, IF */
    READ(0x204, 0x204, 0x5FFF), /* WAITCNT (bit 15, cartridge type, reads 0 on a GBA cartridge) */
    ZERO(0x206, 0x206),
    READ(0x208, 0x208, 0x0001), /* IME */
    ZERO(0x20A, 0x20A),
    READ(0x300, 0x300, 0x0001), /* POSTFLG; HALTCNT (high byte) is write-only and reads 0 */
    ZERO(0x302, 0x302),
};

static io_rule_t table[IO_SIZE / 2];

void io_init_tables(void) {
    for (size_t r = 0; r < sizeof RANGES / sizeof RANGES[0]; r++)
        for (uint32_t off = RANGES[r].first; off <= RANGES[r].last; off += 2)
            table[off >> 1] = (io_rule_t){RANGES[r].kind, RANGES[r].mask};
}

io_rule_t io_rule(uint32_t off) {
    return table[(off & (IO_SIZE - 1)) >> 1];
}
