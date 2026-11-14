package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class SettingsTest {
    @Test
    fun nothingStoredMeansTheDefaultsWhichAreTodaysBehaviour() {
        val s = Settings.load(MemoryStore())
        assertEquals(Settings(), s)
        assertEquals(false, s.smooth)
        assertEquals(Settings.SIZE_FIT, s.size)
        assertEquals(true, s.sound)
        assertEquals(2, s.soundBursts)
        assertEquals(4, s.fastSpeed)
        assertEquals(64L * 1024 * 1024, s.rewindBudget)
        assertEquals(Settings.LOOK_NORMAL, s.touchLook)
        assertEquals(true, s.buzz)
        assertEquals(false, s.biosFile)
        assertEquals(false, s.showSpeed)
        assertEquals(0, s.skin) // Plain
        assertEquals(0, s.skinImport) // a built-in skin
        assertEquals(true, s.boxArt)
        assertEquals(false, s.hardcore) // off until RetroAchievements approves the app (user decision 2026-10-06)
        assertEquals(true, s.leaderboardNotes)
    }

    @Test
    fun everySettingIsSavedAndReadBack() {
        val store = MemoryStore()
        val s = Settings(smooth = true, size = Settings.SIZE_STRETCH, sound = false, delay = Settings.DELAY_HIGH,
            fastSpeed = NativeBridge.FF_MAX, rewind = false, rewindLength = Settings.LENGTH_LONG,
            touchLook = Settings.LOOK_STRONG, buzz = false, biosFile = true, showSpeed = true, skin = 12, skinImport = 3,
            boxArt = false, hardcore = true, leaderboardNotes = false)
        s.save(store)
        assertEquals(s, Settings.load(store))
    }

    @Test
    fun anImportedSkinIsKeptByItsNumberAndANegativeNumberIsIgnored() {
        val store = MemoryStore()
        Settings(skinImport = 3).save(store)
        assertEquals(3, Settings.load(store).skinImport)
        store.put("touch_skin_import", -1)
        assertEquals(0, Settings.load(store).skinImport)
    }

    @Test
    fun theFastForwardSpeedStoredBefore6bIsKept() {
        val store = MemoryStore()
        store.put("fast_speed", 8) // written by the in-game menu row since 5b
        assertEquals(8, Settings.load(store).fastSpeed)
    }

    @Test
    fun aValueThatDoesNotReadIsItsDefault() {
        val store = MemoryStore()
        for ((key, value) in listOf("size" to 9, "delay" to -2, "fast_speed" to 3, "rewind_length" to 7, "touch_look" to 5,
            "smooth" to 4, "touch_skin" to 13)) store.put(key, value)
        assertEquals(Settings(), Settings.load(store))
    }

    @Test
    fun theDelayAndTheRewindLengthMeanBurstsAndBytes() {
        assertEquals(listOf(2, 4, 8), listOf(Settings.DELAY_LOW, Settings.DELAY_MEDIUM, Settings.DELAY_HIGH)
            .map { Settings(delay = it).soundBursts })
        assertEquals(listOf(24L, 64L, 128L).map { it * 1024 * 1024 },
            listOf(Settings.LENGTH_SHORT, Settings.LENGTH_NORMAL, Settings.LENGTH_LONG).map { Settings(rewindLength = it).rewindBudget })
        assertEquals(0L, Settings(rewind = false).rewindBudget)
    }

    @Test
    fun greyGamesAreAutomaticUntilChosenAndAnUnknownLookIsAutomatic() { // 17c
        val store = MemoryStore()
        assertEquals(Looks.AUTO, Settings.load(store).greyLook)
        Settings(greyLook = 0).save(store)
        assertEquals(0, Settings.load(store).greyLook)
        Settings(greyLook = 13).save(store)
        assertEquals(13, Settings.load(store).greyLook)
        store.put("grey_look", 99)
        assertEquals(Looks.AUTO, Settings.load(store).greyLook)
    }

    @Test
    fun eachFamilyKeepsItsOwnImportedSkin() {
        val store = MemoryStore()
        val s = Settings(skinImport = 2, skinImportGb = 5)
        s.save(store)
        assertEquals(s, Settings.load(store))
        assertEquals(2, s.skinImportFor("gba"))
        assertEquals(5, s.skinImportFor("gb"))
        store.put("touch_skin_import_gb", -1)
        assertEquals(0, Settings.load(store).skinImportGb)
    }

    @Test
    fun aSettingsFileFrom120ReadsAsBefore() {
        val store = MemoryStore()
        store.put("touch_skin_import", 3) // 1.2.0 knew only this one: the GBA games' skin
        val s = Settings.load(store)
        assertEquals(3, s.skinImport)
        assertEquals(0, s.skinImportGb)
    }

    @Test
    fun pickingAnImportedSkinTouchesOnlyItsFamily() {
        val s = Settings(skin = 4, skinImport = 2)
        assertEquals(Settings(skin = 4, skinImport = 2, skinImportGb = 5), s.withImported("gb", 5))
        assertEquals(Settings(skin = 4, skinImport = 7), s.withImported("gba", 7))
    }

    @Test
    fun aBuiltInSkinClearsBothImportedChoices() {
        assertEquals(Settings(skin = 6), Settings(skin = 4, skinImport = 2, skinImportGb = 5).withBuiltIn(6))
    }

    @Test
    fun removingASkinClearsItsOwnChoice() {
        val s = Settings(skinImport = 2, skinImportGb = 5)
        assertEquals(Settings(skinImport = 2), s.withoutImported(5))
        assertEquals(Settings(skinImportGb = 5), s.withoutImported(2))
        assertEquals(s, s.withoutImported(9))
    }
}
