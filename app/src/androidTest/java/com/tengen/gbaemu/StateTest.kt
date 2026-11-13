package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Save states on the phone: exact replays through a state, saving/loading a running game, refusals. */
@RunWith(AndroidJUnit4::class)
class StateTest {
    private lateinit var dir: File

    @Before
    fun freshFolder() {
        dir = File(InstrumentationRegistry.getInstrumentation().targetContext.filesDir, "test_states")
        dir.deleteRecursively()
        dir.mkdirs()
    }

    @After
    fun stopSession() = NativeBridge.stop()

    @Test
    fun everyApprovedFingerprintSurvivesAStateHalfWay() {
        val entries = JSONObject(TestRoms.text("expected_frames.json")).getJSONArray("frames")
        assertEquals("approved entries", 38, entries.length())
        val failures = mutableListOf<String>()
        for (i in 0 until entries.length()) {
            val e = entries.getJSONObject(i)
            val frames = e.getInt("frames")
            val got = NativeBridge.debugRunFramesViaState(TestRoms.rom(e.getString("rom")), frames, maxOf(1, frames / 2))
            val parts = got?.split(" ") ?: listOf("state-failed", "")
            val frameOk = parts[0] == e.getString("hash")
            val audioOk = !e.has("audio") || parts[1] == e.getString("audio")
            if (!frameOk || !audioOk) failures += "${e.getString("name")}: got $got"
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun aPausedGameSavesAndLoadsItsMoment() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        NativeBridge.setKeys(KeyMap.UP) // the camera moves, so moments differ
        Thread.sleep(500)
        NativeBridge.pause()
        val state = File(dir, "slot1.state")
        assertTrue("save", NativeBridge.saveState(state.path))
        val saved = NativeBridge.frameBgr555()
        assertNotNull(saved)
        NativeBridge.resume()
        Thread.sleep(500) // play on
        NativeBridge.pause()
        assertFalse("the picture moved on", saved!!.contentEquals(NativeBridge.frameBgr555()!!))
        assertEquals(0, NativeBridge.loadState(state.path))
        assertArrayEquals("back at the saved moment", saved, NativeBridge.frameBgr555())
    }

    @Test
    fun aRunningGameRefusesStates() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(200)
        assertFalse(NativeBridge.saveState(File(dir, "x.state").path))
        assertEquals(NativeBridge.ERR_STATE, NativeBridge.loadState(File(dir, "x.state").path))
    }

    @Test
    fun aStateLoadsIntoANewSessionOfTheSameGameButNotAnother() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(300)
        NativeBridge.pause()
        val state = File(dir, "auto.state")
        assertTrue(NativeBridge.saveState(state.path))
        val saved = NativeBridge.frameBgr555()
        NativeBridge.stop()

        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba"))) // not started: allowed
        assertEquals(0, NativeBridge.loadState(state.path))
        assertArrayEquals(saved, NativeBridge.frameBgr555())
        NativeBridge.stop()

        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/obj_demo.gba")))
        assertEquals("another game's state", NativeBridge.ERR_STATE, NativeBridge.loadState(state.path))
        assertEquals("a missing file", NativeBridge.ERR_STATE, NativeBridge.loadState(File(dir, "none.state").path))
    }
}
