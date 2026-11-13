package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** RetroAchievements on the phone with the app's pretend server (14): sign in, a game's set, an unlock, the hardcore limits. */
@RunWith(AndroidJUnit4::class)
class AchievementsDeviceTest {
    @Before
    fun pretendServer() {
        RaService.worker.send = FakeRaServer::reply
        RaService.worker.start()
    }

    @After
    fun signOut() {
        NativeBridge.stop()
        NativeBridge.raLogout()
        NativeBridge.raSetHardcore(true)
    }

    private fun waitFor(ms: Long, what: () -> Boolean): Boolean {
        val end = System.currentTimeMillis() + ms
        while (System.currentTimeMillis() < end) {
            if (what()) return true
            Thread.sleep(20)
        }
        return what()
    }

    private fun signIn() {
        NativeRa.loginPassword("Tester", "secret")
        assertTrue(waitFor(5000) { NativeBridge.raLoginStatus()[0] == NativeBridge.RA_OK })
    }

    /** m7_demo loaded and its (demo) set loaded before the start. */
    private fun gameWithSet() {
        signIn()
        val rom = TestRoms.rom("tonc/m7_demo.gba")
        assertEquals(0, NativeBridge.loadRom(rom))
        NativeBridge.raLoadGame(rom)
        assertTrue(waitFor(5000) { NativeBridge.raPoll(); NativeBridge.raGameStatus()[0] == NativeBridge.RA_OK })
    }

    @Test
    fun signingInGivesTheToken() {
        signIn()
        assertEquals(listOf("Tester", "TOKEN"), NativeRa.user()!!.toList())
    }

    @Test
    fun aGamesSetLoadsAndUnlocksWhileItRuns() {
        gameWithSet()
        assertEquals(listOf(0, 5), NativeBridge.raSummary()!!.take(2))
        NativeBridge.start()
        var unlocked: RaEvent? = null
        assertTrue(waitFor(20000) {
            generateSequence { RaEvent.of(NativeBridge.raTakeEvent()) }.firstOrNull { it.kind == RaEvent.UNLOCKED }?.let { unlocked = it }
            unlocked != null
        })
        assertEquals("Welcome", unlocked!!.title)
        NativeBridge.pause()
        val list = RaAchievement.list(NativeBridge.raAchievements())
        assertEquals(5, list.size)
        assertTrue(list[0].unlocked)
    }

    @Test
    fun hardcoreRefusesStateLoadsAndCheatsAndSoftcoreAllowsThem() {
        gameWithSet()
        NativeBridge.raSetHardcore(true)
        assertTrue(NativeBridge.raHardcoreActive())
        val state = java.io.File(androidx.test.platform.app.InstrumentationRegistry.getInstrumentation().targetContext.cacheDir, "ra.state")
        assertTrue(NativeBridge.saveState(state.path)) // saving is allowed
        assertEquals(NativeBridge.ERR_STATE, NativeBridge.loadState(state.path))
        assertEquals(NativeBridge.ERR_CHEAT, NativeBridge.setCheats(arrayOf("0203FFF0:12345678"), intArrayOf(NativeBridge.CHEAT_RAW)))
        NativeBridge.start()
        Thread.sleep(200)
        NativeBridge.pause()
        assertFalse(NativeBridge.debugPeek32(0x0203FFF0) == 0x12345678)
        NativeBridge.raSetHardcore(false)
        assertFalse(NativeBridge.raHardcoreActive())
        assertEquals(0, NativeBridge.loadState(state.path))
    }

    @Test
    fun signedOutThereAreNoLimits() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertFalse(NativeBridge.raHardcoreActive())
        assertNotNull(NativeBridge.raUserAgentClause())
        assertEquals(0, NativeBridge.raCanPause())
    }

    @Test
    fun aDemoBoardIsSentAndRankedInHardcore() {
        NativeBridge.raSetHardcore(true) // rcheevos runs boards only in hardcore
        gameWithSet()
        assertTrue(NativeBridge.raHasLeaderboards())
        NativeBridge.start()
        val seen = mutableListOf<RaEvent>()
        assertTrue(waitFor(25000) {
            generateSequence { RaEvent.of(NativeBridge.raTakeEvent()) }.forEach { seen += it }
            seen.any { it.kind == RaEvent.LB_SCOREBOARD }
        })
        NativeBridge.pause()
        assertTrue(seen.any { it.kind == RaEvent.LB_STARTED && it.id == FakeRaServer.SPRINT_ID })
        assertTrue(seen.any { it.kind == RaEvent.TRACKER_SHOW })
        val score = seen.first { it.kind == RaEvent.LB_SCOREBOARD }
        assertEquals("Sprint", score.title)
        assertEquals(4, score.rank)
        assertEquals(31, score.entries)
        assertTrue(score.progress, score.progress.startsWith("0:10."))
    }

    @Test
    fun aBoardsScoresLoadInSoftcoreToo() {
        NativeBridge.raSetHardcore(false)
        gameWithSet()
        assertEquals(listOf("Sprint", "Survival"), RaBoard.list(NativeBridge.raLeaderboards()).map { it.title })
        NativeBridge.raFetchEntries(FakeRaServer.SPRINT_ID)
        assertTrue(waitFor(5000) { NativeBridge.raEntriesStatus()[0] == NativeBridge.RA_OK })
        assertEquals(listOf(NativeBridge.RA_OK, 0, 30, 1), NativeBridge.raEntriesStatus().toList())
        val rows = RaEntry.list(NativeBridge.raEntries())
        assertEquals(15, rows.size)
        assertTrue(rows[10].gapBefore)
        assertEquals(RaEntry(17, true, false, "Tester", rows[12].score), rows[12])
        NativeBridge.raFetchEntries(FakeRaServer.SURVIVAL_ID)
        assertTrue(waitFor(5000) { NativeBridge.raEntriesStatus()[0] == NativeBridge.RA_OK })
        assertEquals(0, NativeBridge.raEntriesStatus()[2])
    }

    /** 17c: a Game Boy game has a session like any other — its set loads, an achievement unlocks, hardcore limits it. */
    @Test
    fun aGameBoyGamesSetLoadsUnlocksAndHardcoreLimitsIt() {
        try {
            for ((file, console) in listOf("../own_roms/gb_demo.gb" to Console.GB, "../own_roms/gbc_demo.gbc" to Console.GBC)) {
                FakeRaServer.console = console.raId
                signIn()
                val rom = TestRoms.rom(file)
                assertEquals(0, NativeBridge.loadRom(rom))
                NativeBridge.raLoadGame(rom)
                assertTrue(waitFor(5000) { NativeBridge.raPoll(); NativeBridge.raGameStatus()[0] == NativeBridge.RA_OK })
                assertEquals(listOf(0, 5), NativeBridge.raSummary()!!.take(2))
                assertTrue(NativeBridge.raHardcoreActive()) // (hardcore is on from the start here: switching it on later waits for a restart)
                assertEquals(NativeBridge.ERR_CHEAT, NativeBridge.setCheats(arrayOf("01AA23C1"), intArrayOf(NativeBridge.CHEAT_AUTO)))
                NativeBridge.start()
                var unlocked: RaEvent? = null
                assertTrue(waitFor(20000) {
                    generateSequence { RaEvent.of(NativeBridge.raTakeEvent()) }.firstOrNull { it.kind == RaEvent.UNLOCKED }?.let { unlocked = it }
                    unlocked != null
                })
                assertEquals("Welcome", unlocked!!.title)
                NativeBridge.pause()
                NativeBridge.raSetHardcore(false)
                assertEquals(0, NativeBridge.setCheats(arrayOf("01AA23C1"), intArrayOf(NativeBridge.CHEAT_AUTO)))
                NativeBridge.stop()
                NativeBridge.raLogout()
                NativeBridge.raSetHardcore(true)
            }
        } finally {
            FakeRaServer.console = Console.GBA.raId
        }
    }
}
