package com.tengen.gbaemu

import kotlin.math.pow
import kotlin.math.roundToInt

/**
 * A skin (8b): the colours of a handheld for the game screen — [body] round the game picture, the D-pad and the buttons
 * (shoulder = L, R, rewind and fast-forward; small = Select and Start), each with its label colour, and [lit] for
 * fast-forward while it is switched on. [rim]: an edge round every button on the body (null = none). [overPicture] /
 * [overPictureText]: the see-through buttons over the picture (null = each part's own colours). [seeThroughBesideGame]:
 * buttons beside the game but inside the picture's box (sideways: on the side bars) are see-through too — Plain, as
 * before 8b; the skins draw them solid on the body. Colours are ARGB. Pure Kotlin, so it is tested on the PC.
 */
data class Skin(
    val id: String, val name: String,
    val body: Int,
    val dpad: Int,
    val ab: Int, val abText: Int,
    val shoulder: Int, val shoulderText: Int,
    val small: Int, val smallText: Int,
    val lit: Int, val litText: Int,
    val rim: Int?,
    val overPicture: Int?, val overPictureText: Int?,
    val seeThroughBesideGame: Boolean = false,
)

object Skins {
    private const val DARK_LIT = 0xFF2A1A02.toInt() // the app's text-on-amber
    private const val AMBER = 0xFFF2A33A.toInt() // the app's amber (AppTheme)

    /** Plain: the touch buttons as before 8b — grey on black, see-through white over the picture. */
    val PLAIN = Skin("plain", "Plain", body = 0xFF000000.toInt(), dpad = 0xFFB4B4B4.toInt(),
        ab = 0xFFB4B4B4.toInt(), abText = 0xFF222222.toInt(), shoulder = 0xFFB4B4B4.toInt(), shoulderText = 0xFF222222.toInt(),
        small = 0xFFB4B4B4.toInt(), smallText = 0xFF222222.toInt(), lit = 0xFFFFD54A.toInt(), litText = 0xFF222222.toInt(),
        rim = null, overPicture = 0xFFFFFFFF.toInt(), overPictureText = 0xFFFFFFFF.toInt(), seeThroughBesideGame = true)

    /** A one-colour handheld: every button [button] with [text], a dark or amber [lit]. */
    private fun skin(id: String, name: String, body: Long, dpad: Long, button: Long, text: Long, amberLit: Boolean = false) =
        skin(id, name, body, dpad, button, text, button, text, button, text, amberLit)

    private fun skin(id: String, name: String, body: Long, dpad: Long, ab: Long, abText: Long, shoulder: Long,
                     shoulderText: Long, small: Long, smallText: Long, amberLit: Boolean) = Skin(
        id, name, body.toInt(), dpad.toInt(), ab.toInt(), abText.toInt(), shoulder.toInt(), shoulderText.toInt(),
        small.toInt(), smallText.toInt(),
        lit = if (amberLit) AMBER else DARK_LIT, litText = if (amberLit) DARK_LIT else AMBER,
        rim = rimOf(body.toInt()), overPicture = null, overPictureText = null)

    /**
     * Every skin, in the order of the Skin page. A skin's place in this list is its number in the settings
     * (`touch_skin`): new skins are only ever added at the end.
     */
    val ALL: List<Skin> = listOf(
        PLAIN,
        skin("ice", "Ice", 0xFFA9D3EA, 0xFF3B3E45, 0xFFDFE3E8, 0xFF333333),
        skin("pink", "Pink", 0xFFE892C0, 0xFF4A3A44, 0xFFF6D9E8, 0xFF5A2A44),
        skin("lime", "Lime", 0xFF8FD07A, 0xFF2E4A2A, 0xFFE6F4DF, 0xFF2E4A2A),
        skin("white", "White", 0xFFEEF0F2, 0xFF2F3136, 0xFFC9CCD2, 0xFF333333),
        skin("black", "Black", 0xFF26272C, 0xFF0E0E10, 0xFF4A4C52, 0xFFDDDDDD, amberLit = true),
        skin("orange", "Orange", 0xFFF08A2C, 0xFF3A2A1E, 0xFFFDE0BF, 0xFF5A3510),
        skin("yellow", "Yellow", 0xFFF3CC2E, 0xFF3A3320, 0xFFFFF2B8, 0xFF5A4A10),
        skin("gold", "Gold", 0xFFD2AE55, 0xFF3A2F14, 0xFFF2E3B0, 0xFF5A4410),
        skin("silver", "Silver", 0xFFB9BDC4, 0xFF3A3D44, 0xFFE9ECF0, 0xFF333333),
        skin("redcream", "Red and cream", 0xFFEBE2CD, 0xFF2B2B2B, 0xFFA61F2A, 0xFFFFFFFF, 0xFFA61F2A, 0xFFFFFFFF,
            0xFF8D1B22, 0xFFFFFFFF, amberLit = true),
        skin("greyred", "Grey and red", 0xFFC9C9C9, 0xFF1E1E1E, 0xFFC8202A, 0xFFFFFFFF, 0xFF2B2B2B, 0xFFDDDDDD,
            0xFF2B2B2B, 0xFFDDDDDD, amberLit = true),
        skin("classic", "Classic grey", 0xFFC4C0B8, 0xFF222222, 0xFF9A2257, 0xFFFFFFFF, 0xFF8F8C86, 0xFF222222,
            0xFF8A8A8A, 0xFF222222, amberLit = false),
    )

    /** The skin with this number (its place in [ALL]); anything else is Plain. */
    fun at(number: Int): Skin = ALL.getOrNull(number) ?: PLAIN

    fun byId(id: String): Skin = ALL.firstOrNull { it.id == id } ?: PLAIN

    /** A part held down: 39 % toward black; a dark colour 25 % toward white instead (Plain: B4B4B4 → 6E6E6E). */
    fun pressed(c: Int): Int = if (luminance(c) < 0.1) mix(c, 0xFFFFFFFF.toInt(), 0.25f) else mix(c, 0xFF000000.toInt(), 0.39f)

    /** The edge round the buttons: the body 40 % toward black; a dark body 30 % toward white. */
    fun rimOf(body: Int): Int = if (luminance(body) < 0.1) mix(body, 0xFFFFFFFF.toInt(), 0.30f) else mix(body, 0xFF000000.toInt(), 0.40f)

    /** [a] moved [t] of the way to [b], opaque. */
    fun mix(a: Int, b: Int, t: Float): Int {
        fun ch(c: Int, shift: Int) = (c shr shift) and 0xFF
        fun m(shift: Int) = (ch(a, shift) * (1 - t) + ch(b, shift) * t).roundToInt()
        return (0xFF shl 24) or (m(16) shl 16) or (m(8) shl 8) or m(0)
    }

    /** WCAG relative luminance (0 black .. 1 white). */
    fun luminance(c: Int): Double {
        fun lin(shift: Int): Double {
            val v = ((c shr shift) and 0xFF) / 255.0
            return if (v <= 0.03928) v / 12.92 else ((v + 0.055) / 1.055).pow(2.4)
        }
        return 0.2126 * lin(16) + 0.7152 * lin(8) + 0.0722 * lin(0)
    }

    /** WCAG contrast ratio, 1 (the same) .. 21 (black on white). */
    fun contrast(a: Int, b: Int): Double {
        val la = luminance(a)
        val lb = luminance(b)
        return (maxOf(la, lb) + 0.05) / (minOf(la, lb) + 0.05)
    }
}
