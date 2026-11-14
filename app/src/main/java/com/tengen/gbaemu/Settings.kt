package com.tengen.gbaemu

/**
 * The app-wide settings (Settings screen). The defaults are the app's behaviour before 6b. Stored in an [IntStore]
 * (the "settings" preferences); `fast_speed` is the key the in-game menu row has used since 5b, so it is kept.
 */
data class Settings(
    val smooth: Boolean = false,
    val size: Int = SIZE_FIT,
    val sound: Boolean = true,
    val delay: Int = DELAY_LOW,
    val fastSpeed: Int = 4,
    val rewind: Boolean = true,
    val rewindLength: Int = LENGTH_NORMAL,
    val touchLook: Int = LOOK_NORMAL,
    val buzz: Boolean = true,
    val biosFile: Boolean = false, // a BIOS file was chosen (Bios.file); the next game starts through it
    val showSpeed: Boolean = false, // the frames per second label on the game screen (7a)
    val skin: Int = 0, // the touch buttons' skin (8b): its number in Skins.ALL; 0 = Plain
    // An imported Delta skin (9): its folder number in files/skins; 0 = the built-in [skin]. One per console family
    // (17c): [skinImport] for GBA games, [skinImportGb] for Game Boy and Game Boy Color games.
    val skinImport: Int = 0,
    val skinImportGb: Int = 0,
    val boxArt: Boolean = true, // box art downloaded for the game list (10b)
    // RetroAchievements' hardcore mode (14): its limits apply only in games with a set. Off by default until
    // RetroAchievements approves the app: until then its server refuses hardcore unlocks (user decision 2026-10-06).
    val hardcore: Boolean = false,
    // Leaderboards (15): the started / failed notes and the live counters over the game; scores always show.
    val leaderboardNotes: Boolean = true,
    val greyLook: Int = Looks.AUTO, // grey Game Boy games' colours (17c): Looks.AUTO or a look's number
) {
    /** AAudio's own buffer, in bursts. */
    val soundBursts: Int get() = when (delay) {
        DELAY_MEDIUM -> 4
        DELAY_HIGH -> 8
        else -> 2
    }

    /** Bytes for rewind snapshots (0 = rewind off). */
    val rewindBudget: Long get() = if (!rewind) 0 else when (rewindLength) {
        LENGTH_SHORT -> 24L * MIB
        LENGTH_LONG -> 128L * MIB
        else -> 64L * MIB
    }

    /** The imported skin chosen for a console [family] ([Console.family]); 0 = none. */
    fun skinImportFor(family: String): Int = if (family == Console.GBA.family) skinImport else skinImportGb

    /** Imported skin [number] chosen for its [family]; the other family's choice stays. */
    fun withImported(family: String, number: Int): Settings =
        if (family == Console.GBA.family) copy(skinImport = number) else copy(skinImportGb = number)

    /** Built-in skin [skin] for every console: back from any imported skin. */
    fun withBuiltIn(skin: Int): Settings = copy(skin = skin, skinImport = 0, skinImportGb = 0)

    /** Imported skin [number] is gone: no family uses it any more. */
    fun withoutImported(number: Int): Settings = copy(skinImport = if (skinImport == number) 0 else skinImport,
        skinImportGb = if (skinImportGb == number) 0 else skinImportGb)

    fun save(store: IntStore) {
        store.put("smooth", flag(smooth))
        store.put("size", size)
        store.put("sound", flag(sound))
        store.put("delay", delay)
        store.put("fast_speed", fastSpeed)
        store.put("rewind", flag(rewind))
        store.put("rewind_length", rewindLength)
        store.put("touch_look", touchLook)
        store.put("buzz", flag(buzz))
        store.put("bios_file", flag(biosFile))
        store.put("show_speed", flag(showSpeed))
        store.put("touch_skin", skin)
        store.put("touch_skin_import", skinImport)
        store.put("touch_skin_import_gb", skinImportGb)
        store.put("box_art", flag(boxArt))
        store.put("ra_hardcore", flag(hardcore))
        store.put("ra_lb_notes", flag(leaderboardNotes))
        store.put("grey_look", greyLook)
    }

    companion object {
        private const val MIB = 1024 * 1024

        const val SIZE_FIT = 0 // the numbers of fit_mode_t (platform/android/fit_rect.h)
        const val SIZE_INTEGER = 1
        const val SIZE_STRETCH = 2
        const val DELAY_LOW = 0
        const val DELAY_MEDIUM = 1
        const val DELAY_HIGH = 2
        const val LENGTH_SHORT = 0
        const val LENGTH_NORMAL = 1
        const val LENGTH_LONG = 2
        const val LOOK_NORMAL = 0
        const val LOOK_FAINT = 1
        const val LOOK_STRONG = 2

        /** The choices' names, by value. */
        val SIZE_NAMES = listOf("Fit screen", "Whole-number steps", "Stretch to fill")
        val DELAY_NAMES = listOf("Low", "Medium", "High")
        val LENGTH_NAMES = listOf("About 1 minute", "About 2½ minutes", "About 5 minutes")
        val LOOK_NAMES = listOf("Normal", "Faint", "Strong")
        val SPEEDS = listOf(2, 4, 8, NativeBridge.FF_MAX)

        fun speedName(speed: Int) = if (speed == NativeBridge.FF_MAX) "Max" else "$speed×"

        fun load(store: IntStore): Settings {
            val d = Settings()
            return Settings(
                smooth = bool(store, "smooth", d.smooth),
                size = choice(store, "size", 0..2, d.size),
                sound = bool(store, "sound", d.sound),
                delay = choice(store, "delay", 0..2, d.delay),
                fastSpeed = store.get("fast_speed")?.takeIf { it in SPEEDS } ?: d.fastSpeed,
                rewind = bool(store, "rewind", d.rewind),
                rewindLength = choice(store, "rewind_length", 0..2, d.rewindLength),
                touchLook = choice(store, "touch_look", 0..2, d.touchLook),
                buzz = bool(store, "buzz", d.buzz),
                biosFile = bool(store, "bios_file", d.biosFile),
                showSpeed = bool(store, "show_speed", d.showSpeed),
                skin = choice(store, "touch_skin", Skins.ALL.indices, d.skin),
                skinImport = store.get("touch_skin_import")?.takeIf { it >= 0 } ?: d.skinImport,
                skinImportGb = store.get("touch_skin_import_gb")?.takeIf { it >= 0 } ?: d.skinImportGb,
                boxArt = bool(store, "box_art", d.boxArt),
                hardcore = bool(store, "ra_hardcore", d.hardcore),
                leaderboardNotes = bool(store, "ra_lb_notes", d.leaderboardNotes),
                greyLook = store.get("grey_look")?.takeIf(Looks::valid) ?: d.greyLook,
            )
        }

        private fun flag(b: Boolean) = if (b) 1 else 0

        private fun bool(store: IntStore, key: String, default: Boolean): Boolean = when (store.get(key)) {
            0 -> false
            1 -> true
            else -> default
        }

        private fun choice(store: IntStore, key: String, valid: IntRange, default: Int): Int =
            store.get(key)?.takeIf { it in valid } ?: default
    }
}
