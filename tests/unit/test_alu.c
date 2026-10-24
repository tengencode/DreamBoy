#include "cpu/alu.h"
#include "test.h"

TEST(alu_add_carry_and_overflow) {
    alu_result_t r = alu_add(0xFFFFFFFF, 1, 0);
    CHECK_EQ(r.value, 0);
    CHECK(r.carry);
    CHECK(!r.overflow);
    r = alu_add(0x7FFFFFFF, 1, 0);
    CHECK_EQ(r.value, 0x80000000);
    CHECK(!r.carry);
    CHECK(r.overflow);
    r = alu_add(5, ~5u, 1); /* 5 - 5 */
    CHECK_EQ(r.value, 0);
    CHECK(r.carry); /* no borrow */
    r = alu_add(3, ~5u, 1); /* 3 - 5 */
    CHECK_EQ(r.value, 0xFFFFFFFEu);
    CHECK(!r.carry);
    r = alu_add(0x80000000, ~1u, 1); /* INT_MIN - 1 */
    CHECK(r.overflow);
}

TEST(alu_shift_by_immediate_special_encodings) {
    bool c = true;
    CHECK_EQ(shift_by_imm(0, 0x12345678, 0, &c), 0x12345678); /* LSL #0: unchanged */
    CHECK(c);
    c = false;
    CHECK_EQ(shift_by_imm(0, 0x80000001, 1, &c), 2);
    CHECK(c);
    c = false;
    CHECK_EQ(shift_by_imm(1, 0x80000000, 0, &c), 0); /* LSR #0 means #32 */
    CHECK(c);
    c = false;
    CHECK_EQ(shift_by_imm(2, 0x80000000, 0, &c), 0xFFFFFFFF); /* ASR #0 means #32 */
    CHECK(c);
    c = true;
    CHECK_EQ(shift_by_imm(3, 0x00000002, 0, &c), 0x80000001); /* ROR #0 means RRX */
    CHECK(!c);
    c = true;
    CHECK_EQ(shift_by_imm(3, 0x000000F1, 4, &c), 0x1000000F);
    CHECK(!c);
}

TEST(alu_shift_by_register_large_amounts) {
    bool c = true;
    CHECK_EQ(shift_by_reg(0, 0x1234, 0, &c), 0x1234); /* amount 0: unchanged, C kept */
    CHECK(c);
    c = false;
    CHECK_EQ(shift_by_reg(0, 1, 32, &c), 0); /* LSL 32: C = bit 0 */
    CHECK(c);
    c = true;
    CHECK_EQ(shift_by_reg(0, 1, 33, &c), 0);
    CHECK(!c);
    c = false;
    CHECK_EQ(shift_by_reg(1, 0x80000000, 32, &c), 0); /* LSR 32: C = bit 31 */
    CHECK(c);
    c = true;
    CHECK_EQ(shift_by_reg(1, 0x80000000, 33, &c), 0);
    CHECK(!c);
    c = false;
    CHECK_EQ(shift_by_reg(2, 0x80000000, 40, &c), 0xFFFFFFFF);
    CHECK(c);
    c = false;
    CHECK_EQ(shift_by_reg(3, 0x80000001, 32, &c), 0x80000001); /* ROR 32: C = bit 31 */
    CHECK(c);
    c = false;
    CHECK_EQ(shift_by_reg(3, 0x00000003, 33, &c), 0x80000001); /* ROR 33 = ROR 1 */
    CHECK(c);
}

TEST(alu_multiplier_cycles) {
    CHECK_EQ(alu_mul_cycles(0x000000FF, false), 1);
    CHECK_EQ(alu_mul_cycles(0x00001234, false), 2);
    CHECK_EQ(alu_mul_cycles(0x00123456, false), 3);
    CHECK_EQ(alu_mul_cycles(0x12345678, false), 4);
    CHECK_EQ(alu_mul_cycles(0xFFFFFF00, true), 1);
    CHECK_EQ(alu_mul_cycles(0xFFFFFF00, false), 4);
}
