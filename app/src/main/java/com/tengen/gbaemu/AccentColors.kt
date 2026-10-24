package com.tengen.gbaemu

import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * The Game Pak look's colours (12) as plain ARGB numbers, so the contrast rules are tested on the PC. Light: grey
 * cartridge plastic; dark: smoked plastic. The slot and the label band are dark in both.
 */
object Palette {
    class Roles(
        val plastic: Int, val cart: Int, val cartHigh: Int, val highlight: Int, val shade: Int,
        val slot: Int, val slotMouth: Int, val band: Int, val bandEdge: Int,
        val ink: Int, val inkSoft: Int, val outline: Int, val error: Int, val onError: Int, val errorSoft: Int,
    )

    /** Label stock: the pale printed paper of labels and save stickers — print, so the same in both themes, with [LIGHT]'s inks. */
    val LABEL_STOCK: Int = 0xFFF2F2F4.toInt()

    val LIGHT = Roles(
        plastic = 0xFFC5C7CC.toInt(), cart = 0xFFD7D9DD.toInt(), cartHigh = 0xFFE3E4E7.toInt(),
        highlight = 0xFFEEEFF1.toInt(), shade = 0xFFA3A5AB.toInt(),
        slot = 0xFF26272B.toInt(), slotMouth = 0xFF141518.toInt(), band = 0xFF141518.toInt(), bandEdge = 0xFF141518.toInt(),
        ink = 0xFF16171B.toInt(), inkSoft = 0xFF44464D.toInt(), outline = 0xFF8E9097.toInt(),
        error = 0xFF9E0E27.toInt(), onError = 0xFFFFFFFF.toInt(), errorSoft = 0xFFF0D5D9.toInt(),
    )

    val DARK = Roles(
        plastic = 0xFF141518.toInt(), cart = 0xFF222327.toInt(), cartHigh = 0xFF2C2D32.toInt(),
        highlight = 0xFF34353B.toInt(), shade = 0xFF070708.toInt(),
        slot = 0xFF0A0A0C.toInt(), slotMouth = 0xFF000000.toInt(), band = 0xFF000000.toInt(), bandEdge = 0xFF34353A.toInt(),
        ink = 0xFFECECEF.toInt(), inkSoft = 0xFFA6A8AF.toInt(), outline = 0xFF4A4C52.toInt(),
        error = 0xFFFF8C98.toInt(), onError = 0xFF3A0710.toInt(), errorSoft = 0xFF3A1A1F.toInt(),
    )
}

/**
 * The accent follows the console skin (12), like the see-through coloured cartridges: each built-in skin names a
 * seed colour; the accent keeps its hue and saturation and only its lightness moves until it reads well — as a
 * button with text on it, and as text itself on every ground (the hardest: the plastic in the light theme, the
 * lightest card in the dark one). Plain, White, Black, Silver and imported skins use the default indigo.
 */
object AccentColors {
    const val DEFAULT_SEED = 0xFF4B3FB8.toInt()
    private const val WHITE = 0xFFFFFFFF.toInt()

    private val SEEDS: Map<String, Int> = mapOf(
        "ice" to 0xFFA9D3EA, "pink" to 0xFFE892C0, "lime" to 0xFF8FD07A, "orange" to 0xFFF08A2C,
        "yellow" to 0xFFF3CC2E, "gold" to 0xFFD2AE55, "redcream" to 0xFFA61F2A, "greyred" to 0xFFC8202A,
        "classic" to 0xFF9A2257,
    ).mapValues { it.value.toInt() }

    /** The seed for a built-in skin's id; null (an imported skin) or a skin without one: the default. */
    fun seedFor(skinId: String?): Int = SEEDS[skinId] ?: DEFAULT_SEED

    /** An accent and the colour of text and icons on it. */
    data class Accent(val color: Int, val on: Int)

    /** Light theme: darker until white on it is ≥ 4.5:1 and it is ≥ 4.5:1 on the plastic (the darkest light ground). */
    fun light(seed: Int): Accent {
        val (h, s, l0) = hsl(seed)
        var l = l0
        var c = seed
        while (l > 0f && (Skins.contrast(WHITE, c) < 4.5 || Skins.contrast(c, Palette.LIGHT.plastic) < 4.5)) {
            l -= 0.01f
            c = fromHsl(h, s, l)
        }
        return Accent(c, WHITE)
    }

    /** Dark theme: lighter until it is ≥ 4.5:1 on the lightest card (the lightest dark ground); text on it the same hue, very dark. */
    fun dark(seed: Int): Accent {
        val (h, s, l0) = hsl(seed)
        var l = l0
        var c = seed
        while (l < 1f && Skins.contrast(c, Palette.DARK.cartHigh) < 4.5) {
            l += 0.01f
            c = fromHsl(h, s, l)
        }
        var ol = 0.16f
        var on = fromHsl(h, s, ol)
        while (ol > 0f && Skins.contrast(on, c) < 4.5) {
            ol -= 0.02f
            on = fromHsl(h, s, ol)
        }
        return Accent(c, on)
    }

    /** Hue (0..360), saturation and lightness (0..1). */
    fun hsl(c: Int): Triple<Float, Float, Float> {
        val r = ((c shr 16) and 0xFF) / 255f
        val g = ((c shr 8) and 0xFF) / 255f
        val b = (c and 0xFF) / 255f
        val max = maxOf(r, g, b)
        val min = minOf(r, g, b)
        val l = (max + min) / 2
        val d = max - min
        if (d == 0f) return Triple(0f, 0f, l)
        val s = d / (1 - abs(2 * l - 1))
        val h = when (max) {
            r -> 60 * (((g - b) / d) % 6)
            g -> 60 * ((b - r) / d + 2)
            else -> 60 * ((r - g) / d + 4)
        }
        return Triple((h + 360) % 360, s, l)
    }

    fun fromHsl(h: Float, s: Float, l: Float): Int {
        val lc = l.coerceIn(0f, 1f)
        val c = (1 - abs(2 * lc - 1)) * s
        val x = c * (1 - abs((h / 60) % 2 - 1))
        val m = lc - c / 2
        val (r, g, b) = when ((h / 60).toInt()) {
            0 -> Triple(c, x, 0f)
            1 -> Triple(x, c, 0f)
            2 -> Triple(0f, c, x)
            3 -> Triple(0f, x, c)
            4 -> Triple(x, 0f, c)
            else -> Triple(c, 0f, x)
        }
        fun ch(v: Float) = ((v + m) * 255).roundToInt().coerceIn(0, 255)
        return (0xFF shl 24) or (ch(r) shl 16) or (ch(g) shl 8) or ch(b)
    }
}
