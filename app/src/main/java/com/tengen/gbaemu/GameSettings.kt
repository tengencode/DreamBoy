package com.tengen.gbaemu

/**
 * Each game's own settings (Game settings… in its hold menu), by game id in an [IntStore] (the "game_settings"
 * preferences): its save type and clock, [AUTO] = what the app detects. Also what was detected the last time the
 * game ran without overrides, to show beside Auto.
 */
class GameSettings(private val store: IntStore) {
    data class Game(val saveType: Int = AUTO, val clock: Int = AUTO)

    fun of(id: String): Game = Game(
        store.get("$id.save")?.takeIf { it in SAVE_CHOICES } ?: AUTO,
        store.get("$id.clock")?.takeIf { it == CLOCK_OFF || it == CLOCK_ON } ?: AUTO,
    )

    fun set(id: String, game: Game) {
        store.put("$id.save", game.saveType.takeIf { it != AUTO })
        store.put("$id.clock", game.clock.takeIf { it != AUTO })
    }

    /** The detected save type and whether a clock was found, or null if the game has not run yet. */
    fun detected(id: String): Pair<Int, Boolean>? {
        val save = store.get("$id.detected_save") ?: return null
        return save to (store.get("$id.detected_clock") == 1)
    }

    /** After a game opened without overrides: what the app found ([hardware] = NativeBridge.HW_* bits). */
    fun remember(id: String, saveType: Int, hardware: Int) {
        store.put("$id.detected_save", saveType)
        store.put("$id.detected_clock", if (hardware and NativeBridge.HW_RTC != 0) 1 else 0)
    }

    companion object {
        const val AUTO = -1
        const val SAVE_NONE = 0 // the numbers of gba_save_type_t (core/include/gba.h)
        const val SAVE_UNKNOWN = 1
        const val SAVE_SRAM = 2
        const val SAVE_FLASH64 = 3
        const val SAVE_FLASH128 = 4
        const val SAVE_EEPROM = 5
        const val SAVE_EEPROM512 = 6
        const val SAVE_EEPROM8K = 7
        const val CLOCK_OFF = 0
        const val CLOCK_ON = 1

        /** The save types the user can pick (not "unknown" or "EEPROM, size not known"). */
        val SAVE_CHOICES = listOf(SAVE_NONE, SAVE_SRAM, SAVE_FLASH64, SAVE_FLASH128, SAVE_EEPROM512, SAVE_EEPROM8K)

        fun saveName(type: Int): String = when (type) {
            SAVE_NONE -> "None"
            SAVE_UNKNOWN -> "Not known yet"
            SAVE_SRAM -> "SRAM"
            SAVE_FLASH64 -> "Flash 64K"
            SAVE_FLASH128 -> "Flash 128K"
            SAVE_EEPROM -> "EEPROM"
            SAVE_EEPROM512 -> "EEPROM 512 B"
            SAVE_EEPROM8K -> "EEPROM 8K"
            else -> "Auto"
        }
    }
}
