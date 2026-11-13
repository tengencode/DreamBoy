package com.tengen.gbaemu

/** Why a game did not open, in the player's words ([code]: what NativeBridge.loadGame returned, not 0). */
fun loadErrorText(code: Int): String =
    if (code == NativeBridge.ERR_UNSUPPORTED) "This cartridge type isn't supported yet." else NOT_A_GAME

const val NOT_A_GAME = "Not a game this app can play."

/** What Game Boy and Game Boy Color games do not have yet (17b): said where the player would look for it. */
const val NO_ACHIEVEMENTS_YET = "No achievements for Game Boy games yet"

/**
 * Which console a game is for. [native] is the number the C side uses (NativeBridge.CONSOLE_*); [frameWidth] x
 * [frameHeight] its picture; [hasShoulders]: it has L and R buttons; [mark] is printed on the game's label.
 */
enum class Console(val native: Int, val frameWidth: Int, val frameHeight: Int, val hasShoulders: Boolean, val mark: String) {
    GBA(0, 240, 160, true, "GBA"),
    GB(1, 160, 144, false, "GB"),
    GBC(2, 160, 144, false, "GBC");

    /** The cheat types' names by number (17c): each core has its own (gba_cheat_type_t, gb_cheat_type_t); 0 is Auto. */
    val cheatTypes: List<String> get() = if (this == GBA) CHEAT_TYPES else GB_CHEAT_TYPES

    /** Game Boy and Game Boy Color games share their buttons and layouts: one family; GBA is the other. */
    val family: String get() = if (this == GBA) "gba" else "gb"

    companion object {
        fun ofNative(n: Int): Console? = entries.firstOrNull { it.native == n }

        /** By the file name's ending alone (`.gba`, `.gb`, `.gbc`, any case); null for anything else. */
        fun ofName(fileName: String): Console? = when {
            fileName.endsWith(".gba", ignoreCase = true) -> GBA
            fileName.endsWith(".gbc", ignoreCase = true) -> GBC
            fileName.endsWith(".gb", ignoreCase = true) -> GB
            else -> null
        }

        /**
         * The same rule as the C side's machine_detect. By content first: a Game Boy Advance header (its fixed byte
         * and checksum), else a Game Boy header (its checksum, and a size byte that fits the file; bit 7 of byte
         * 0x143 = Game Boy Color). Only when neither fits, by [fileName]'s ending — and then the colour bit still
         * tells a Game Boy game from a Game Boy Color one. Null: not a game of these consoles as far as can be told.
         */
        fun of(rom: ByteArray, fileName: String?): Console? {
            if (gbaHeader(rom)) return GBA
            val color = rom.size > 0x143 && rom[0x143].toInt() and 0x80 != 0
            if (gbHeader(rom)) return if (color) GBC else GB
            return when (fileName?.let(::ofName)) {
                GBA -> GBA
                GB, GBC -> if (color) GBC else GB
                null -> null
            }
        }

        private fun gbaHeader(rom: ByteArray): Boolean {
            if (rom.size < 0xC0 || rom[0xB2] != 0x96.toByte()) return false
            var sum = 0
            for (i in 0xA0..0xBC) sum -= rom[i]
            return (sum - 0x19).toByte() == rom[0xBD]
        }

        private fun gbHeader(rom: ByteArray): Boolean {
            if (rom.size < 0x150) return false
            val code = rom[0x148].toInt() and 0xFF
            if (code > 8 || 0x8000L shl code != rom.size.toLong()) return false
            var sum = 0
            for (i in 0x134..0x14C) sum = sum - rom[i] - 1
            return sum.toByte() == rom[0x14D]
        }
    }
}
