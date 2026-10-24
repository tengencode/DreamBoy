package com.tengen.gbaemu

import android.view.KeyEvent
import com.tengen.gbaemu.MotionMath.RumbleAction
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.abs

class MotionMathTest {
    private val g = 9.80665f
    private val full = MotionMath.FULL

    @Test
    fun flatIsLevel() {
        assertEquals(0 to 0, MotionMath.tilt(0f, 0f, 0))
        assertEquals(0 to 0, MotionMath.tilt(0f, 0f, 1))
    }

    @Test
    fun uprightScreenEdgesGiveFullTilt() {
        // Right edge lowered: the accelerometer's x reads -g. Top edge lowered: y reads -g.
        assertEquals(full to 0, MotionMath.tilt(-g, 0f, 0))
        assertEquals(-full to 0, MotionMath.tilt(g, 0f, 0))
        assertEquals(0 to -full, MotionMath.tilt(0f, -g, 0))
        assertEquals(0 to full, MotionMath.tilt(0f, g, 0))
    }

    @Test
    fun sidewaysScreenSwapsTheAxes() {
        // ROTATION_90 (the device's top edge to the left): the screen's right edge is the device's bottom edge.
        assertEquals(full to 0, MotionMath.tilt(0f, g, 1))
        assertEquals(0 to -full, MotionMath.tilt(-g, 0f, 1))
        // ROTATION_270: the other way round.
        assertEquals(full to 0, MotionMath.tilt(0f, -g, 3))
        assertEquals(0 to -full, MotionMath.tilt(g, 0f, 3))
        // Upside down.
        assertEquals(full to 0, MotionMath.tilt(g, 0f, 2))
    }

    @Test
    fun beyondOneGIsClamped() {
        assertEquals(full to -full, MotionMath.tilt(-3 * g, -3 * g, 0))
    }

    @Test
    fun halfwayIsHalf() {
        val (x, _) = MotionMath.tilt(-g / 2, 0f, 0)
        assertTrue("$x", abs(x - full / 2) <= 1)
    }

    @Test
    fun turnIsClockwisePositiveAndTwoTurnsPerSecondIsFull() {
        assertEquals(full, MotionMath.turn(-(4 * PI).toFloat()))
        assertEquals(-full, MotionMath.turn((4 * PI).toFloat()))
        assertEquals(0, MotionMath.turn(0f))
        assertEquals(full, MotionMath.turn(-100f)) // clamped
    }

    @Test
    fun lightScaleEnds() {
        assertEquals(0, MotionMath.light(0f))
        assertEquals(0, MotionMath.light(10f))
        assertEquals(255, MotionMath.light(100_000f))
        assertEquals(255, MotionMath.light(1_000_000f))
        assertEquals(128, MotionMath.light(1_000f)) // 255 × 2/4 = 127.5
    }

    @Test
    fun aHeldKeyOverridesOnlyItsAxis() {
        assertEquals(1234, MotionMath.withKeys(1234, minusHeld = false, plusHeld = false, keyValue = 24000))
        assertEquals(24000, MotionMath.withKeys(1234, minusHeld = false, plusHeld = true, keyValue = 24000))
        assertEquals(-24000, MotionMath.withKeys(1234, minusHeld = true, plusHeld = false, keyValue = 24000))
        assertEquals(0, MotionMath.withKeys(1234, minusHeld = true, plusHeld = true, keyValue = 24000))
    }

    @Test
    fun motionKeysAreIJKLQE() {
        assertEquals(MotionMath.KEY_I, MotionMath.motionKey(KeyEvent.KEYCODE_I))
        assertEquals(MotionMath.KEY_J, MotionMath.motionKey(KeyEvent.KEYCODE_J))
        assertEquals(MotionMath.KEY_K, MotionMath.motionKey(KeyEvent.KEYCODE_K))
        assertEquals(MotionMath.KEY_L, MotionMath.motionKey(KeyEvent.KEYCODE_L))
        assertEquals(MotionMath.KEY_Q, MotionMath.motionKey(KeyEvent.KEYCODE_Q))
        assertEquals(MotionMath.KEY_E, MotionMath.motionKey(KeyEvent.KEYCODE_E))
        assertEquals(0, MotionMath.motionKey(KeyEvent.KEYCODE_X)) // a GBA button, not motion
        for (b in KeyMap.bindings) for (code in b.keyboard.keys) assertEquals(0, MotionMath.motionKey(code))
    }

    @Test
    fun rumbleActsOnlyOnChanges() {
        val s = MotionMath.RumbleSwitch()
        assertEquals(RumbleAction.NOTHING, s.next(false))
        assertEquals(RumbleAction.START, s.next(true))
        assertEquals(RumbleAction.NOTHING, s.next(true))
        assertEquals(RumbleAction.STOP, s.next(false))
        assertEquals(RumbleAction.NOTHING, s.next(false))
        s.next(true)
        s.reset()
        assertEquals(RumbleAction.START, s.next(true))
    }
}
