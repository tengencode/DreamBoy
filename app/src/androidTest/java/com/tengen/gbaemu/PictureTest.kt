package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith

/** The real OpenGL drawing code, read back pixel by pixel. */
@RunWith(AndroidJUnit4::class)
class PictureTest {
    /** A frame where neighbouring pixels differ in every channel; white and black included. */
    private val frame = ShortArray(240 * 160) { i ->
        val x = i % 240
        val y = i / 240
        ((x * 7 + y * 3) and 31 or (((x + y * 5) and 31) shl 5) or (((x * 3 + y) and 31) shl 10)).toShort()
    }.also {
        it[0] = 0x7FFF
        it[1] = 0
    }

    /** BGR555 → the RGBA int glReadPixels gives: each 5-bit channel c becomes (c << 3) | (c >> 2). */
    private fun rgba(p: Short): Int {
        val v = p.toInt() and 0x7FFF
        fun ch(c: Int) = (c shl 3) or (c shr 2)
        return ch(v and 31) or (ch((v shr 5) and 31) shl 8) or (ch((v shr 10) and 31) shl 16) or (0xFF shl 24)
    }

    private val black = 0xFF shl 24

    @Test
    fun doubleSizeGivesEachGbaPixelA2x2Block() {
        val out = NativeBridge.debugDrawFrame(frame, 480, 320)
        assertNotNull("offscreen OpenGL ES 3 drawing failed", out)
        for (y in 0 until 320) for (x in 0 until 480) {
            assertEquals("pixel ($x, $y)", rgba(frame[(y / 2) * 240 + x / 2]), out!![y * 480 + x])
        }
    }

    @Test
    fun wideSurfaceGetsBlackBarsAndACentredPicture() {
        val out = NativeBridge.debugDrawFrame(frame, 720, 320)
        assertNotNull("offscreen OpenGL ES 3 drawing failed", out)
        for (y in 0 until 320) for (x in 0 until 720) {
            val want = if (x < 120 || x >= 600) black else rgba(frame[(y / 2) * 240 + (x - 120) / 2])
            assertEquals("pixel ($x, $y)", want, out!![y * 720 + x])
        }
    }

    @Test
    fun theBarsAreTheSkinsBodyColour() { // 8b: Ice's body A9D3EA round the picture
        val ice = Skins.byId("ice").body
        val out = NativeBridge.debugDrawFrameOn(frame, 720, 320, Settings.SIZE_FIT, false, ice)
        assertNotNull("offscreen OpenGL ES 3 drawing failed", out)
        val iceRgba = 0xA9 or (0xD3 shl 8) or (0xEA shl 16) or (0xFF shl 24)
        for (y in 0 until 320) for (x in 0 until 720) {
            val want = if (x < 120 || x >= 600) iceRgba else rgba(frame[(y / 2) * 240 + (x - 120) / 2])
            assertEquals("pixel ($x, $y)", want, out!![y * 720 + x])
        }
    }
}
