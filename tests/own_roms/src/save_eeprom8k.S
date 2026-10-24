@ save_eeprom8k: EEPROM test with an 8 KiB chip (14-bit block addresses). See eeprom.inc.
    .equ ADDR_BITS, 14
    .equ LAST_BLOCK, 1023
    .include "common.inc"
    .include "eeprom.inc"
