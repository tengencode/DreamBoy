package com.tengen.gbaemu

import org.junit.Assert.assertTrue
import org.junit.Test

/** The Game Pak colours (12): every text colour reads at ≥ 4.5:1 on every ground it sits on, in both themes. */
class PaletteTest {
    private val white = 0xFFFFFFFF.toInt()

    private fun readable(what: String, fg: Int, bg: Int) {
        val c = Skins.contrast(fg, bg)
        assertTrue("$what: %.2f:1".format(c), c >= 4.5)
    }

    @Test
    fun textReadsOnEveryGroundInBothThemes() {
        for ((theme, p) in listOf("light" to Palette.LIGHT, "dark" to Palette.DARK)) {
            for ((name, ground) in listOf("plastic" to p.plastic, "cart" to p.cart, "cartHigh" to p.cartHigh)) {
                readable("$theme ink on $name", p.ink, ground)
                readable("$theme inkSoft on $name", p.inkSoft, ground)
                readable("$theme error on $name", p.error, ground)
            }
            readable("$theme text on error", p.onError, p.error)
            readable("$theme error on its soft ground", p.error, p.errorSoft)
            readable("$theme white on the label band", white, p.band)
            readable("$theme white on the slot", white, p.slot)
        }
    }

    @Test
    fun printedLabelsAndStickersAreReadableInEitherTheme() {
        // Label stock is print: the same in both themes, with the light theme's inks on it.
        readable("ink on label stock", Palette.LIGHT.ink, Palette.LABEL_STOCK)
        readable("inkSoft on label stock", Palette.LIGHT.inkSoft, Palette.LABEL_STOCK)
    }

    @Test
    fun theSlotAndTheLabelBandAreDarkInBothThemes() {
        for (p in listOf(Palette.LIGHT, Palette.DARK)) {
            assertTrue(Skins.luminance(p.slot) < 0.03)
            assertTrue(Skins.luminance(p.band) < 0.03)
        }
        assertTrue(Skins.luminance(Palette.LIGHT.plastic) > 0.4) // grey plastic by day
        assertTrue(Skins.luminance(Palette.DARK.plastic) < 0.02) // smoked plastic at night
    }
}
