package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LooksTest {
    @Test
    fun theListStartsWithAutomaticAndGrey() {
        assertEquals(14, Looks.NAMES.size) // the core's fixed looks (gb_look_count)
        assertEquals(listOf(Looks.AUTO) + (0..13), Looks.CHOICES)
        assertEquals("Automatic", Looks.name(Looks.AUTO))
        assertEquals("Grey", Looks.name(0))
        assertEquals("Original green", Looks.name(1))
        assertEquals("Inverted", Looks.name(13))
        assertEquals("Same as in Settings", Looks.name(Looks.SAME))
    }

    @Test
    fun onlyTheListedNumbersAreLooks() {
        assertTrue(Looks.valid(Looks.AUTO) && Looks.valid(0) && Looks.valid(13))
        assertFalse(Looks.valid(14) || Looks.valid(Looks.SAME) || Looks.valid(-3))
    }

    @Test
    fun aGamesOwnLookWinsOverTheSetting() {
        val s = Settings(greyLook = 5)
        assertEquals(5, Looks.forGame(s, GameSettings.Game()))
        assertEquals(Looks.AUTO, Looks.forGame(s, GameSettings.Game(look = Looks.AUTO)))
        assertEquals(2, Looks.forGame(s, GameSettings.Game(look = 2)))
    }

    @Test
    fun aCoreColourBecomesAScreenColour() {
        assertEquals(0xFFFFFFFF.toInt(), Looks.argb(0x7FFF))
        assertEquals(0xFF000000.toInt(), Looks.argb(0))
        assertEquals(0xFFFF0000.toInt(), Looks.argb(0x001F)) // red is the lowest five bits
        assertEquals(0xFF0000FF.toInt(), Looks.argb(0x7C00))
    }
}
