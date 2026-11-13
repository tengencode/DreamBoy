package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Which console a ROM is for: the same cases as the C side's tests (tests/unit/test_machine.c). */
class ConsoleTest {
    /** A 0x200-byte ROM with a valid Game Boy Advance header. */
    private fun gbaRom() = ByteArray(0x200).also { rom ->
        "TESTGAME".toByteArray().copyInto(rom, 0xA0)
        rom[0xB2] = 0x96.toByte()
        var sum = 0
        for (i in 0xA0..0xBC) sum -= rom[i]
        rom[0xBD] = (sum - 0x19).toByte()
    }

    /** A ROM with a valid Game Boy header: the size byte fits [size], the checksum is right. */
    private fun gbRom(size: Int = 0x8000, cgbFlag: Int = 0) = ByteArray(size).also { rom ->
        "TEST".toByteArray().copyInto(rom, 0x134)
        rom[0x143] = cgbFlag.toByte()
        rom[0x148] = Integer.numberOfTrailingZeros(size / 0x8000).toByte()
        fixChecksum(rom)
    }

    private fun fixChecksum(rom: ByteArray) {
        var sum = 0
        for (i in 0x134..0x14C) sum = sum - rom[i] - 1
        rom[0x14D] = sum.toByte()
    }

    @Test
    fun theContentDecidesWhateverTheFileIsCalled() {
        assertEquals(Console.GBA, Console.of(gbaRom(), "game.gb"))
        assertEquals(Console.GBA, Console.of(gbaRom(), null))
        assertEquals(Console.GB, Console.of(gbRom(), "game.gba"))
        assertEquals(Console.GB, Console.of(gbRom(), "game.zip"))
        assertEquals(Console.GBC, Console.of(gbRom(0x10000, 0xC0), "game.gba"))
        assertEquals(Console.GBC, Console.of(gbRom(cgbFlag = 0x80), null))
    }

    @Test
    fun withoutAUsableHeaderTheNameDecides() {
        val rom = gbRom()
        rom[0x14D] = (rom[0x14D] + 1).toByte() // a wrong header checksum (homebrew, hacks)
        assertEquals(Console.GB, Console.of(rom, "x.GB"))
        assertEquals(Console.GB, Console.of(rom, "x.gbc")) // the colour flag still comes from the header
        rom[0x143] = 0x80.toByte()
        assertEquals(Console.GBC, Console.of(rom, "x.gb"))
        assertEquals(Console.GBA, Console.of(rom, "x.gba"))
        assertNull(Console.of(rom, "x.bin"))
        assertNull(Console.of(rom, "gb"))
        assertNull(Console.of(rom, null))
        assertEquals(Console.GB, Console.of(ByteArray(0), "x.gb")) // too short to read: the core will refuse it
        val lying = gbRom().also { it[0x148] = 3; fixChecksum(it) } // a size byte that does not fit the file
        assertEquals(Console.GBA, Console.of(lying, "x.gba"))
    }

    @Test
    fun namesAndNumbers() {
        assertEquals(Console.GBA, Console.ofName("a.GBA"))
        assertEquals(Console.GB, Console.ofName("a.gb"))
        assertEquals(Console.GBC, Console.ofName("a.Gbc"))
        assertNull(Console.ofName("a.zip"))
        assertEquals(Console.GBC, Console.ofNative(NativeBridge.CONSOLE_GBC))
        assertEquals(Console.GB, Console.ofNative(NativeBridge.CONSOLE_GB))
        assertEquals(Console.GBA, Console.ofNative(NativeBridge.CONSOLE_GBA))
        assertNull(Console.ofNative(-1))
        assertEquals("gb", Console.GBC.family)
        assertEquals("gb", Console.GB.family)
        assertEquals("gba", Console.GBA.family)
        assertEquals(160 to 144, Console.GB.frameWidth to Console.GB.frameHeight)
        assertEquals(false, Console.GBC.hasShoulders)
    }
}
