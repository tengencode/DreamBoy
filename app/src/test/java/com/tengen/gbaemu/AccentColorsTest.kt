package com.tengen.gbaemu

import kotlin.math.abs
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** The accent follows the console skin (12) and stays readable in both themes. */
class AccentColorsTest {
    private val skins: List<String?> = Skins.ALL.map { it.id } + listOf(null) // null: an imported Delta skin

    private fun readable(what: String, fg: Int, bg: Int) {
        val c = Skins.contrast(fg, bg)
        assertTrue("$what: %.2f:1".format(c), c >= 4.5)
    }

    @Test
    fun everySkinsAccentReadsAsAButtonAndAsTextInBothThemes() {
        for (id in skins) {
            val seed = AccentColors.seedFor(id)
            val light = AccentColors.light(seed)
            readable("$id light: text on the accent", light.on, light.color)
            for (ground in listOf(Palette.LIGHT.plastic, Palette.LIGHT.cart, Palette.LIGHT.cartHigh)) {
                readable("$id light: the accent on the ground", light.color, ground)
            }
            val dark = AccentColors.dark(seed)
            readable("$id dark: text on the accent", dark.on, dark.color)
            for (ground in listOf(Palette.DARK.plastic, Palette.DARK.cart, Palette.DARK.cartHigh)) {
                readable("$id dark: the accent on the ground", dark.color, ground)
            }
        }
    }

    @Test
    fun theColouredSkinsGiveTheirOwnHueAndTheOthersTheDefault() {
        for (id in listOf("plain", "white", "black", "silver", null)) assertEquals(AccentColors.DEFAULT_SEED, AccentColors.seedFor(id))
        for (id in listOf("ice", "pink", "lime", "orange", "yellow", "gold", "redcream", "greyred", "classic")) {
            val seed = AccentColors.seedFor(id)
            assertTrue(id, seed != AccentColors.DEFAULT_SEED)
            for (accent in listOf(AccentColors.light(seed), AccentColors.dark(seed))) {
                val dh = abs(AccentColors.hsl(seed).first - AccentColors.hsl(accent.color).first)
                assertTrue("$id keeps its hue (moved ${dh}°)", minOf(dh, 360 - dh) < 6)
            }
        }
    }

    @Test
    fun hslRoundTrips() {
        for (c in listOf(0xFF4B3FB8.toInt(), 0xFFA9D3EA.toInt(), 0xFFF3CC2E.toInt(), 0xFF000000.toInt(), 0xFFFFFFFF.toInt(), 0xFF808080.toInt())) {
            val (h, s, l) = AccentColors.hsl(c)
            val back = AccentColors.fromHsl(h, s, l)
            for (shift in listOf(16, 8, 0)) assertTrue(abs(((c shr shift) and 0xFF) - ((back shr shift) and 0xFF)) <= 1)
        }
    }
}
