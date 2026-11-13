package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class GameSettingsTest {
    @Test
    fun aGameWithNothingStoredIsAuto() {
        val g = GameSettings(MemoryStore()).of("e86b3224")
        assertEquals(GameSettings.AUTO, g.saveType)
        assertEquals(GameSettings.AUTO, g.clock)
    }

    @Test
    fun eachGameKeepsItsOwn() {
        val store = MemoryStore()
        val gs = GameSettings(store)
        gs.set("e86b3224", GameSettings.Game(GameSettings.SAVE_FLASH128, GameSettings.CLOCK_OFF))
        gs.set("04f79685", GameSettings.Game(GameSettings.SAVE_SRAM, GameSettings.AUTO))
        val again = GameSettings(store)
        assertEquals(GameSettings.Game(GameSettings.SAVE_FLASH128, GameSettings.CLOCK_OFF), again.of("e86b3224"))
        assertEquals(GameSettings.Game(GameSettings.SAVE_SRAM, GameSettings.AUTO), again.of("04f79685"))
        assertEquals(GameSettings.Game(), again.of("00000000"))
        again.set("e86b3224", GameSettings.Game()) // back to Auto
        assertEquals(GameSettings.Game(), GameSettings(store).of("e86b3224"))
    }

    @Test
    fun whatTheGameHadWhenDetectedIsRemembered() {
        val store = MemoryStore()
        val gs = GameSettings(store)
        assertEquals(null, gs.detected("e86b3224"))
        gs.remember("e86b3224", GameSettings.SAVE_FLASH128, NativeBridge.HW_RTC or NativeBridge.HW_RUMBLE)
        assertEquals(GameSettings.SAVE_FLASH128 to true, GameSettings(store).detected("e86b3224"))
    }

    @Test
    fun aStoredValueThatDoesNotReadIsAuto() {
        val store = MemoryStore()
        store.put("e86b3224.save", 1) // "unknown" is not a choice
        store.put("e86b3224.clock", 7)
        assertEquals(GameSettings.Game(), GameSettings(store).of("e86b3224"))
    }

    @Test
    fun aGreyGamesLookIsKeptAndSameIsNotStored() { // 17c
        val store = MemoryStore()
        val gs = GameSettings(store)
        assertEquals(Looks.SAME, gs.of("aaaa0001").look)
        gs.set("aaaa0001", GameSettings.Game(look = 3))
        gs.set("bbbb0002", GameSettings.Game(look = Looks.AUTO))
        assertEquals(3, GameSettings(store).of("aaaa0001").look)
        assertEquals(Looks.AUTO, GameSettings(store).of("bbbb0002").look)
        gs.set("aaaa0001", GameSettings.Game())
        assertEquals(null, store.get("aaaa0001.look"))
        store.put("aaaa0001.look", 99) // from a newer version: not a look here
        assertEquals(Looks.SAME, GameSettings(store).of("aaaa0001").look)
    }
}
