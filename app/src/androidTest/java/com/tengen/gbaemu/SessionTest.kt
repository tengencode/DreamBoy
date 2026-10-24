package com.tengen.gbaemu

import android.graphics.PixelFormat
import android.media.ImageReader
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.nio.ByteBuffer
import kotlin.math.abs
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The emulation and render threads: speed, pause/resume, window hand-over, bad ROMs. */
@RunWith(AndroidJUnit4::class)
class SessionTest {
    @After
    fun stopSession() = NativeBridge.stop()

    @Test
    fun runsAtGbaSpeed() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(500)
        val startFrames = NativeBridge.debugFrameCount()
        val startMs = SystemClock.elapsedRealtime()
        Thread.sleep(5000)
        val frames = NativeBridge.debugFrameCount() - startFrames
        val expected = (SystemClock.elapsedRealtime() - startMs) * 59.7275 / 1000.0
        assertTrue("ran $frames frames, expected about $expected", abs(frames - expected) <= 3.0)
    }

    @Test
    fun survivesTwentyStartPauseResumeStopRounds() {
        repeat(20) { round ->
            assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
            NativeBridge.start()
            Thread.sleep(40)
            NativeBridge.pause()
            Thread.sleep(20) /* a frame already running when pause came may still finish */
            val paused = NativeBridge.debugFrameCount()
            Thread.sleep(60)
            assertEquals("round $round: frames while paused", paused, NativeBridge.debugFrameCount())
            NativeBridge.resume()
            Thread.sleep(60)
            assertTrue("round $round: no frames after resume", NativeBridge.debugFrameCount() > paused)
            NativeBridge.stop()
        }
    }

    @Test
    fun drawsIntoAWindowAndLetsGoOfIt() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("jsmolka/stripes.gba"))) /* coloured from the first frames */
        NativeBridge.start()
        val reader = ImageReader.newInstance(480, 320, PixelFormat.RGBA_8888, 3)
        assertTrue("render thread could not draw into the window", NativeBridge.setSurface(reader.surface))
        Thread.sleep(500)
        val image = reader.acquireLatestImage()
        assertNotNull("nothing was drawn", image)
        val row = image!!.planes[0].buffer
        var lit = 0
        for (i in 0 until 480 * 4 step 4) if ((row.get(160 * image.planes[0].rowStride + i).toInt() and 0xFF) != 0) lit++
        image.close()
        assertTrue("the middle row is all black", lit > 0)
        assertTrue(NativeBridge.setSurface(null))
        reader.close()
    }

    @Test
    fun rejectsAnEmptyRom() {
        assertNotEquals(0, NativeBridge.loadRom(ByteBuffer.allocateDirect(0)))
    }
}
