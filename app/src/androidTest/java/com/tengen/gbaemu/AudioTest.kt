package com.tengen.gbaemu

import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.math.abs
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Sound through AAudio: the stream, sound pacing, pause, the clock fallback, reopening, the watchdog. */
@RunWith(AndroidJUnit4::class)
class AudioTest {
    private val ACTIVE = 0
    private val RATE = 1
    private val BURST = 2
    private val PLAYED = 3
    private val SILENT = 4
    private val LOUD = 5
    private val REOPENS = 6

    @After
    fun stopSession() = NativeBridge.stop()

    private fun load(rom: String) = assertEquals(0, NativeBridge.loadRom(TestRoms.rom(rom)))

    private fun stats() = NativeBridge.debugAudioStats()

    /** Frames the emulation runs in 5 s, measured after a warm-up, against 59.7275 per second. */
    private fun assertGbaSpeed() {
        Thread.sleep(500)
        val startFrames = NativeBridge.debugFrameCount()
        val startMs = SystemClock.elapsedRealtime()
        Thread.sleep(5000)
        val frames = NativeBridge.debugFrameCount() - startFrames
        val expected = (SystemClock.elapsedRealtime() - startMs) * 59.7275 / 1000.0
        assertTrue("ran $frames frames, expected about $expected", abs(frames - expected) <= 3.0)
    }

    @Test
    fun streamOpensOnTheEmulator() {
        load("tonc/m7_demo.gba")
        NativeBridge.start()
        Thread.sleep(300)
        val s = stats()
        assertEquals("sound active", 1L, s[ACTIVE])
        assertTrue("sample rate ${s[RATE]}", s[RATE] > 0)
        assertTrue("burst ${s[BURST]}", s[BURST] > 0)
    }

    @Test
    fun runsAtGbaSpeedPacedBySound() {
        load("tonc/m7_demo.gba")
        NativeBridge.start()
        assertGbaSpeed()
        assertEquals("sound active", 1L, stats()[ACTIVE])
    }

    @Test
    fun soundReallyPlays() {
        load("../own_roms/snd_psg.gba")
        NativeBridge.start()
        Thread.sleep(500)
        val a = stats()
        val startMs = SystemClock.elapsedRealtime()
        Thread.sleep(2500)
        val b = stats()
        val expected = (SystemClock.elapsedRealtime() - startMs) * a[RATE] / 1000.0
        val played = b[PLAYED] - a[PLAYED]
        assertTrue("played $played frames, expected about $expected", abs(played - expected) <= expected * 0.05)
        assertTrue("nothing but silence", b[LOUD] > 0)
        assertEquals("silence-filled frames while playing", 0L, b[SILENT] - a[SILENT])
    }

    @Test
    fun pauseStopsTheSoundAndResumeRestartsIt() {
        load("../own_roms/snd_psg.gba")
        NativeBridge.start()
        Thread.sleep(500)
        NativeBridge.pause()
        Thread.sleep(100)
        val paused = stats()[PLAYED]
        Thread.sleep(300)
        assertEquals("frames played while paused", paused, stats()[PLAYED])
        NativeBridge.resume()
        Thread.sleep(300)
        assertTrue("no sound after resume", stats()[PLAYED] > paused)
    }

    @Test
    fun withoutSoundTheClockPacesTheGame() {
        load("tonc/m7_demo.gba")
        NativeBridge.debugSetAudioEnabled(false)
        NativeBridge.start()
        assertGbaSpeed()
        assertEquals("sound active", 0L, stats()[ACTIVE])
    }

    @Test
    fun aLostSoundDeviceReopensTheStream() {
        load("../own_roms/snd_psg.gba")
        NativeBridge.start()
        Thread.sleep(500)
        NativeBridge.debugSimulateAudioDisconnect()
        Thread.sleep(500)
        val a = stats()
        assertEquals("reopens", 1L, a[REOPENS])
        assertEquals("sound active", 1L, a[ACTIVE])
        Thread.sleep(300)
        assertTrue("no sound after reopening", stats()[PLAYED] > a[PLAYED])
    }

    @Test
    fun aStuckStreamIsReopenedByTheWatchdog() {
        load("../own_roms/snd_psg.gba")
        NativeBridge.start()
        Thread.sleep(2500) /* past the watchdog's 2 s start-up grace */
        NativeBridge.debugSimulateStuckStream()
        Thread.sleep(800) /* 250 ms without draining, then the reopen */
        val a = stats()
        assertEquals("reopens", 1L, a[REOPENS])
        assertEquals("sound active", 1L, a[ACTIVE])
        Thread.sleep(300)
        assertTrue("no sound after reopening", stats()[PLAYED] > a[PLAYED])
    }
}
