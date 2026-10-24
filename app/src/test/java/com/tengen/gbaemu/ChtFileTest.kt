package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class ChtFileTest {
    /** Two cheats as libretro-database's GBA files write them (read 2026-10-04). */
    private val real = """
        cheats = 19

        cheat0_desc = "Infinite HP"
        cheat0_code = "42000538+07CF+00000004+014C"
        cheat0_enable = false

        cheat1_desc = "Master Code"
        cheat1_code = "D8BAE4D9+4864DCE5+A86CDBA5+19BA49B3"
        cheat1_enable = false
    """.trimIndent()

    @Test
    fun realFilesGiveNamesAndCodesAsLines() {
        assertEquals(listOf(
            "Infinite HP" to "42000538 07CF\n00000004 014C",
            "Master Code" to "D8BAE4D9 4864DCE5\nA86CDBA5 19BA49B3",
        ), ChtFile.parse(real))
    }

    @Test
    fun quotesBareValuesAndSpacesRoundTheEqualsSign() {
        val text = listOf(
            "# a comment = not a cheat",
            "cheat0_desc=\"Say \\\"hi\\\" C:\\\\x\"",
            "cheat0_code=\"0200A000:63\"",
            "just some words",
            "cheat1_desc   =   Bare name",
            "cheat1_code = 42000538+07CF",
        ).joinToString("\n")
        assertEquals(listOf("Say \"hi\" C:\\x" to "0200A000:63", "Bare name" to "42000538 07CF"), ChtFile.parse(text))
    }

    @Test
    fun windowsLineEndsAndAByteOrderMark() {
        assertEquals(ChtFile.parse(real), ChtFile.parse("\uFEFF" + real.replace("\n", "\r\n")))
    }

    @Test
    fun cheatsComeInNumberOrderAndNeedACode() {
        val text = """
            cheat2_desc = "Third"
            cheat2_code = "42000538+07CF"
            cheat0_desc = "First"
            cheat0_code = "D8BAE4D9+4864DCE5"
            cheat1_desc = "No code"
            cheat3_desc = "Empty code"
            cheat3_code = ""
            cheat4_code = "0200A000:63"
            cheat5_desc = ""
            cheat5_code = "0200A001:01"
            cheat6_desc = "Memory search"
            cheat6_address = "33816576"
            cheat6_handler = "1"
            cheat6_value = "99"
        """.trimIndent()
        assertEquals(listOf(
            "First" to "D8BAE4D9 4864DCE5",
            "Third" to "42000538 07CF",
            "Cheat 5" to "0200A000:63",
            "Cheat 6" to "0200A001:01",
        ), ChtFile.parse(text))
    }

    @Test
    fun aFileThatIsNotACheatFileGivesNothing() {
        val romBytes = String(ByteArray(4096) { (it * 31 + 7).toByte() }, Charsets.UTF_8)
        assertEquals(emptyList<Pair<String, String>>(), ChtFile.parse(romBytes))
        assertEquals(emptyList<Pair<String, String>>(), ChtFile.parse(""))
        assertEquals(emptyList<Pair<String, String>>(), ChtFile.parse("cheats = 0\n"))
    }

    @Test
    fun codePartsPairAnAddressWithItsValue() {
        assertEquals("42000538 07CF", ChtFile.codeLines("42000538+07CF"))
        assertEquals("D8BAE4D9 4864DCE5", ChtFile.codeLines("D8BAE4D9+4864DCE5"))
        assertEquals("42000538 07CF\n00000004", ChtFile.codeLines("42000538+07CF+00000004")) // an odd count: the last alone
        assertEquals("0200A000:63\n42000538 07CF", ChtFile.codeLines("0200A000:63+42000538+07CF"))
        assertEquals("820258400063", ChtFile.codeLines("8202 5840\t0063")) // spaces inside a part removed
        assertEquals("42000538 07CF", ChtFile.codeLines("+42000538++07CF+")) // empty parts dropped
        assertEquals("ABCDEFGH\n07CF", ChtFile.codeLines("ABCDEFGH+07CF")) // not hex: no pair
        assertEquals("", ChtFile.codeLines(" + "))
    }

    private val mine = Cheat("Walk through walls", NativeBridge.CHEAT_CODEBREAKER, "82025840 0063", true)

    @Test
    fun newCheatsGoAfterTheGamesOwnSwitchedOffAndAuto() {
        val m = ChtFile.merge(listOf(mine), listOf("A" to "42000538 07CF", "B" to "D8BAE4D9 4864DCE5")) { true }
        assertEquals(listOf(mine,
            Cheat("A", NativeBridge.CHEAT_AUTO, "42000538 07CF", false),
            Cheat("B", NativeBridge.CHEAT_AUTO, "D8BAE4D9 4864DCE5", false)), m.cheats)
        assertEquals(ChtFile.Merge(m.cheats, added = 2, unreadable = 0, already = 0), m)
    }

    @Test
    fun duplicatesAndUnreadableCheatsAreSkippedAndCounted() {
        val found = listOf(
            "Walk through walls" to "82025840 0063",  // in the list: spaces and case don't matter
            "Walk through walls" to "8202584O 0063",   // another code (a letter O): new, but unreadable
            "Money" to "42000538 07cf",
            "Money" to "4200053807CF",                 // the same inside the file
            "Bad" to "XYZ",
        )
        val readable = { codes: String -> "O" !in codes && codes != "XYZ" }
        val m = ChtFile.merge(listOf(mine.copy(codes = "8202 5840\n0063")), found, readable)
        assertEquals(listOf("Walk through walls", "Money"), m.cheats.map { it.name })
        assertEquals("42000538 07cf", m.cheats[1].codes) // kept as the file wrote it
        assertEquals(1, m.added)
        assertEquals(2, m.unreadable)
        assertEquals(2, m.already)
    }

    @Test
    fun theNoteSaysWhatHappened() {
        fun note(added: Int, unreadable: Int, already: Int) = ChtFile.note(ChtFile.Merge(emptyList(), added, unreadable, already))
        assertEquals("Added 72 cheats.", note(72, 0, 0))
        assertEquals("Added 1 cheat.", note(1, 0, 3))
        assertEquals("Added 70 cheats. 2 couldn't be read.", note(70, 2, 0))
        assertEquals("Added 5 cheats. 1 couldn't be read.", note(5, 1, 0))
        assertEquals("No new cheats in this file.", note(0, 0, 4))
        assertEquals("No new cheats in this file. 1 couldn't be read.", note(0, 1, 4))
        assertEquals("No cheats could be read from this file.", note(0, 3, 0))
        assertEquals("This file has no cheats.", ChtFile.NO_CHEATS)
    }
}
