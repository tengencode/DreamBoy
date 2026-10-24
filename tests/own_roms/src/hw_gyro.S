@ hw_gyro: a gyro + rumble cartridge (gyro + rumble in the core's game list, by its game code).
@ Turns the rumble motor (GPIO pin 3) on for 30 frames, then off for 30 frames, for ever.
@ The GPIO registers are left unreadable: they sit at 0x080000C4-0x080000C9, inside wait_frames (common.inc),
@ and once readable the CPU would fetch them instead of that code. Rumble needs no reads.
@ game code: RZWT
    .include "common.inc"
    rom_start
    ldr r4, =0x080000C4 @ GPIO data; +2 direction
    mov r0, #8
    strh r0, [r4, #2]   @ pin 3 (the motor) is an output
loop:
    mov r0, #8
    strh r0, [r4]       @ motor on
    frames 30
    mov r0, #0
    strh r0, [r4]       @ motor off
    frames 30
    b loop
