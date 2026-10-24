@ save_eeprom512: EEPROM test with a 512-byte chip (6-bit block addresses). See eeprom.inc.
    .equ ADDR_BITS, 6
    .equ LAST_BLOCK, 63
    .include "common.inc"
    .include "eeprom.inc"
