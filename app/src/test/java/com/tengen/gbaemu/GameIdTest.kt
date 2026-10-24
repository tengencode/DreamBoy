package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class GameIdTest {
    @Test
    fun isTheStandardCrc32InLowercaseHex() {
        assertEquals("cbf43926", GameId.of("123456789".toByteArray())) // the CRC-32 check value
        assertEquals("00000000", GameId.of(ByteArray(0)))
    }

    @Test
    fun differentRomsGetDifferentIds() {
        val rom = ByteArray(1024) { it.toByte() }
        val patched = rom.copyOf().also { it[500] = 99 }
        assertEquals(8, GameId.of(rom).length)
        assert(GameId.of(rom) != GameId.of(patched))
    }
}
