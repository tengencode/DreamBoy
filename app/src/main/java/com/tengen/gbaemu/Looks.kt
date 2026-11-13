package com.tengen.gbaemu

/**
 * Looks for grey Game Boy games (17c): the colours their four shades are shown in. The numbers are the core's
 * (core_gb/include/gb.h: GB_LOOK_AUTO, then the fixed looks 0 .. gb_look_count()-1); Game Boy Color games ignore them.
 */
object Looks {
    const val AUTO = -1 // the colours a Game Boy Color gives the game; grey for a game it doesn't know
    const val SAME = -2 // a game's own setting: none, the one in Settings applies

    /** The fixed looks' names, by number: ours first, then the Game Boy Color's own. */
    val NAMES = listOf("Grey", "Original green", "Pocket", "Brown", "Red", "Dark brown", "Pastel mix", "Orange", "Yellow",
        "Blue", "Dark blue", "Green", "Dark green", "Inverted")

    /** What the player picks from, in the list's order. */
    val CHOICES = listOf(AUTO) + NAMES.indices

    const val AUTO_HELP = "Automatic: the colours a Game Boy Color gives each game. Games it doesn't know stay grey."

    fun valid(look: Int) = look == AUTO || look in NAMES.indices

    fun name(look: Int): String = when {
        look == SAME -> "Same as in Settings"
        look in NAMES.indices -> NAMES[look]
        else -> "Automatic"
    }

    /** The look a game is shown in: its own, or the one from Settings. */
    fun forGame(settings: Settings, game: GameSettings.Game): Int = if (valid(game.look)) game.look else settings.greyLook

    /** A core colour (15 bits, red lowest) as an opaque screen colour. */
    fun argb(c: Int): Int {
        fun wide(v: Int) = (v shl 3) or (v shr 2)
        return (0xFF shl 24) or (wide(c and 31) shl 16) or (wide(c shr 5 and 31) shl 8) or wide(c shr 10 and 31)
    }
}
