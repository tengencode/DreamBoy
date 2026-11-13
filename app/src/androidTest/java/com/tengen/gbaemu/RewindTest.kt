package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Fast-forward and rewind on the phone: exact replays through a rewind, the speeds, rewinding a running game. */
@RunWith(AndroidJUnit4::class)
class RewindTest {
    @After
    fun stopSession() = NativeBridge.stop()

    @Test
    fun everyApprovedFingerprintSurvivesARewind() {
        val entries = JSONObject(TestRoms.text("expected_frames.json")).getJSONArray("frames")
        assertEquals("approved entries", 38, entries.length())
        val failures = mutableListOf<String>()
        for (i in 0 until entries.length()) {
            val e = entries.getJSONObject(i)
            val frames = e.getInt("frames")
            // As CMake's rewind_<name> tests: at two thirds (short runs: the last frame but one), back half the snapshots, at least 2.
            val at = (frames * 2 / 3).let { if (it < 9) frames - 1 else it }
            val steps = maxOf(2, at / 4 / 2)
            val got = NativeBridge.debugRunFramesViaRewind(TestRoms.rom(e.getString("rom")), frames, at, steps)
            val parts = got?.split(" ") ?: listOf("rewind-failed", "")
            val frameOk = parts[0] == e.getString("hash")
            val audioOk = !e.has("audio") || parts[1] == e.getString("audio")
            if (!frameOk || !audioOk) failures += "${e.getString("name")}: got $got"
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    /** Frames the game runs in [ms] at the given fast-forward speed. */
    private fun framesIn(ms: Long, speed: Int): Long {
        NativeBridge.setFastForward(speed)
        Thread.sleep(300) // settle
        val from = NativeBridge.debugFrameCount()
        Thread.sleep(ms)
        return NativeBridge.debugFrameCount() - from
    }

    @Test
    fun fastForwardRunsThatManyTimesFaster() { // by the clock (no sound)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.debugSetAudioEnabled(false)
        NativeBridge.start()
        val normal = framesIn(1000, 0)
        val double = framesIn(1000, 2)
        val four = framesIn(1000, 4)
        val max = framesIn(1000, NativeBridge.FF_MAX)
        val back = framesIn(1000, 0)
        assertTrue("normal $normal", normal in 50..70)
        assertTrue("2x $double vs $normal", double in (normal * 17 / 10)..(normal * 23 / 10))
        assertTrue("4x $four vs $normal", four >= normal * 3)
        assertTrue("max $max vs 4x $four", max >= four)
        assertTrue("back to normal $back", back in 50..70)
    }

    @Test
    fun fastForwardWithSoundIsPacedByTheSound() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        val normal = framesIn(1000, 0)
        val double = framesIn(1000, 2)
        assertTrue("normal $normal", normal in 50..70)
        assertTrue("2x $double vs $normal", double in (normal * 17 / 10)..(normal * 23 / 10))
        assertEquals("sound still playing", 1L, NativeBridge.debugAudioStats()[0])
    }

    @Test
    fun rewindingStepsBackToTheOldestSnapshotAndThenPlaysOn() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.debugSetAudioEnabled(false)
        NativeBridge.start()
        NativeBridge.setKeys(KeyMap.UP)
        Thread.sleep(1000)
        NativeBridge.setKeys(0)
        val snaps = NativeBridge.debugRewindStats()[0]
        assertTrue("snapshots after 1 s: $snaps", snaps in 10..20)
        NativeBridge.setRewinding(true)
        // Rewinding starts at the emulation thread's next loop step: a frame already running still finishes (on a
        // real phone a frame can take long enough for that to happen nearly every time), so count from the first step.
        val deadline = System.nanoTime() + 1_000_000_000L
        while (NativeBridge.debugRewindStats()[2] == 0L && System.nanoTime() < deadline) Thread.sleep(1)
        val framesBefore = NativeBridge.debugFrameCount()
        Thread.sleep(snaps * 1000 / 60 + 700) // one step per 1/60 s, then some more at the oldest
        val stats = NativeBridge.debugRewindStats()
        assertEquals("no frames run while rewinding", framesBefore, NativeBridge.debugFrameCount())
        assertEquals("only the oldest snapshot left", 1L, stats[0])
        assertTrue("steps ${stats[2]} for $snaps snapshots", stats[2] in (snaps - 1)..(snaps + 2)) // a snapshot may come before rewinding starts
        NativeBridge.setRewinding(false)
        Thread.sleep(500)
        assertTrue("plays on", NativeBridge.debugFrameCount() > framesBefore + 20)
    }
}
