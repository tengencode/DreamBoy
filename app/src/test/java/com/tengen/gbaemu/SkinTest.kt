package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

class SkinTest {
    @Test
    fun thirteenSkinsWithTheirOwnIdsAndNamesPlainFirst() {
        assertEquals(13, Skins.ALL.size)
        assertEquals(13, Skins.ALL.map { it.id }.toSet().size)
        assertEquals(13, Skins.ALL.map { it.name }.toSet().size)
        assertSame(Skins.PLAIN, Skins.ALL[0])
        assertEquals(listOf("Plain", "Ice", "Pink", "Lime", "White", "Black", "Orange", "Yellow", "Gold", "Silver",
            "Red and cream", "Grey and red", "Classic grey"), Skins.ALL.map { it.name })
    }

    @Test
    fun anUnknownNumberOrIdIsPlain() {
        assertSame(Skins.PLAIN, Skins.at(-1))
        assertSame(Skins.PLAIN, Skins.at(13))
        assertSame(Skins.PLAIN, Skins.byId("purple"))
        assertEquals("ice", Skins.at(1).id)
        assertEquals("classic", Skins.byId("classic").id)
    }

    @Test
    fun plainIsTheLookBefore8b() {
        val p = Skins.PLAIN
        assertEquals(0xFF000000.toInt(), p.body)
        for (c in listOf(p.dpad, p.ab, p.shoulder, p.small)) {
            assertEquals(0xFFB4B4B4.toInt(), c)
            assertEquals(0xFF6E6E6E.toInt(), Skins.pressed(c))
        }
        for (c in listOf(p.abText, p.shoulderText, p.smallText)) assertEquals(0xFF222222.toInt(), c)
        assertEquals(0xFFFFFFFF.toInt(), p.overPicture)
        assertEquals(0xFFFFFFFF.toInt(), p.overPictureText)
        assertEquals(0xFFFFD54A.toInt(), p.lit)
        assertNull(p.rim)
        assertTrue(p.seeThroughBesideGame) // sideways, every button see-through as before 8b
        for (s in Skins.ALL.drop(1)) assertFalse(s.name, s.seeThroughBesideGame)
    }

    @Test
    fun everyLabelIsReadableOnItsButton() {
        for (s in Skins.ALL) {
            for ((what, button, text) in listOf(Triple("A/B", s.ab, s.abText), Triple("L/R", s.shoulder, s.shoulderText),
                Triple("Select/Start", s.small, s.smallText), Triple("lit", s.lit, s.litText))) {
                val c = Skins.contrast(button, text)
                assertTrue("${s.name} $what: $c", c >= 4.5)
            }
        }
    }

    @Test
    fun everyButtonHasAnEdgeOnTheBodyAndLitShowsAgainstIdle() {
        for (s in Skins.ALL.drop(1)) {
            val rim = s.rim!!
            assertTrue("${s.name} rim", Skins.contrast(rim, s.body) >= 2.0)
            assertTrue("${s.name} lit", Skins.contrast(s.lit, s.shoulder) >= 2.0)
        }
    }

    @Test
    fun pressedIsDarkerOrForADarkColourLighter() {
        assertEquals(0xFF6E6E6E.toInt(), Skins.pressed(0xFFB4B4B4.toInt()))
        assertTrue(Skins.luminance(Skins.pressed(0xFFDFE3E8.toInt())) < Skins.luminance(0xFFDFE3E8.toInt()))
        assertTrue(Skins.luminance(Skins.pressed(0xFF0E0E10.toInt())) > Skins.luminance(0xFF0E0E10.toInt()))
    }
}
