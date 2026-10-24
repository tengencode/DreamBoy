package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class GameNamesTest {
    private val dat = """
        clrmamepro (
        	name "Nintendo - Game Boy Advance"
        	description "Nintendo - Game Boy Advance"
        	version "2026.08.01"
        )

        game (
        	name "2 in 1 Game Pack - Spider-Man + Spider-Man 2 (USA) (En,Fr,De+En,Fr,De,Es)"
        	rom ( name "2 in 1 Game Pack - Spider-Man + Spider-Man 2 (USA) (En,Fr,De+En,Fr,De,Es).gba" size 33554432 crc 3ABC30BD md5 DDCA8CBB )
        )

        game (
        	name "Sample Quest - Green Version (USA, Europe)"
        	region "USA"
        	serial "BPEE"
        	rom ( name "Sample Quest - Green Version (USA, Europe).gba" size 16777216 crc 1F1C08FB md5 605B89B6 serial "BPEE" )
        )

        game (
        	name "Sample Quest - Red Version (USA, Europe) (Rev 1)"
        	region "USA"
        	serial "AXVE"
        	rom ( name "Sample Quest - Red Version (USA, Europe) (Rev 1).gba" size 16777216 crc 61641576 md5 E0503182 serial "AXVE" )
        )

        game (
        	name "Sample Quest - Red Version (USA, Europe)"
        	region "USA"
        	serial "AXVE"
        	rom ( name "Sample Quest - Red Version (USA, Europe).gba" size 16777216 crc F0815EE7 md5 53D1A202 serial "AXVE" )
        )

        game (
        	name "Sample Quest - Red Version (USA, Europe) (Rev 2)"
        	region "USA"
        	serial "AXVE"
        	rom ( name "Sample Quest - Red Version (USA, Europe) (Rev 2).gba" size 16777216 crc AEAC73E6 md5 3E1B2065 serial "AXVE" )
        )
    """.trimIndent()

    private val names = GameNames.fromDat(dat)

    @Test
    fun aRealExcerptIsReadAndGamesAreFoundByFingerprint() {
        assertEquals(5, names.rows.size)
        assertEquals("Sample Quest - Green Version (USA, Europe)", names.find("1f1c08fb", ""))
        assertEquals("Sample Quest - Green Version (USA, Europe)", names.find("1F1C08FB", "")) // any case
        assertEquals("Sample Quest - Red Version (USA, Europe) (Rev 2)", names.find("aeac73e6", "AXVE")) // the fingerprint first
        assertEquals(GameNames.Row("1f1c08fb", "BPEE", "Sample Quest - Green Version (USA, Europe)"), names.rows[1])
    }

    @Test
    fun aHackIsFoundByItsCode() {
        assertEquals("Sample Quest - Green Version (USA, Europe)", names.find("12345678", "BPEE"))
        assertNull(names.find("12345678", "ZZZZ"))
        assertNull(names.find("12345678", ""))
    }

    @Test
    fun theplainestNameWinsWhenGamesShareACode() {
        assertEquals("Sample Quest - Red Version (USA, Europe)", names.find("12345678", "AXVE"))
    }

    @Test
    fun aRomWithoutACodeIsFoundOnlyByFingerprint() {
        assertEquals("2 in 1 Game Pack - Spider-Man + Spider-Man 2 (USA) (En,Fr,De+En,Fr,De,Es)", names.find("3abc30bd", ""))
        assertEquals("", names.rows[0].code)
        assertEquals(emptyList<GameNames.Row>(), GameNames.fromDat("not a list at all\n)\ngame (\n").rows)
    }

    @Test
    fun theKeptCopyReadsBackTheSame() {
        val back = GameNames.fromTsv("#time\t123\n" + names.toTsv() + "broken line\n\n")
        assertEquals(names.rows, back.rows)
        assertEquals("Sample Quest - Red Version (USA, Europe)", back.find("", "AXVE"))
    }

    @Test
    fun theCodeComesFromTheHeader() {
        fun rom(code: String) = ByteArray(0xC0).also { code.toByteArray(Charsets.ISO_8859_1).copyInto(it, 0xAC) }
        assertEquals("BPEE", GameNames.code(rom("BPEE")))
        assertEquals("A2B9", GameNames.code(rom("A2B9")))
        assertEquals("", GameNames.code(rom("bpee"))) // capitals and digits only
        assertEquals("", GameNames.code(rom("AB")))   // two zero bytes after
        assertEquals("", GameNames.code(ByteArray(0xAE)))
    }

    @Test
    fun pictureNamesReplaceLibretrosSymbols() {
        assertEquals("Sample Quest - Green Version (USA, Europe)", GameNames.pictureName("Sample Quest - Green Version (USA, Europe)"))
        assertEquals("Lilo _ Stitch_ A_B_c_d_e_f_g_h_i", GameNames.pictureName("Lilo & Stitch: A/B*c`d<e>f?g\\h|i"))
    }
}
