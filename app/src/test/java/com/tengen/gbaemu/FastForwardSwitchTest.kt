package com.tengen.gbaemu

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FastForwardSwitchTest {
    @Test
    fun aTapSwitchesItOnAndTheNextTapOff() {
        val s = FastForwardSwitch()
        assertFalse(s.fast)
        s.press(1000)
        assertTrue(s.fast) // fast at once, before knowing if it is a tap
        s.release(1150)
        assertTrue(s.fast && s.switchedOn)
        s.press(5000)
        s.release(5100)
        assertFalse(s.fast || s.switchedOn)
    }

    @Test
    fun holdingIsFastOnlyWhileHeld() {
        val s = FastForwardSwitch()
        s.press(1000)
        assertTrue(s.fast && !s.switchedOn)
        s.release(1000 + FastForwardSwitch.TAP_MS)
        assertFalse(s.fast)
    }

    @Test
    fun holdingWhileSwitchedOnEndsOff() {
        val s = FastForwardSwitch()
        s.press(0)
        s.release(100) // on
        s.press(2000)
        assertTrue(s.fast)
        s.release(3000) // a hold: back to normal speed
        assertFalse(s.fast || s.switchedOn)
    }

    @Test
    fun aSecondPressWhileHeldAndAStrayReleaseChangeNothing() {
        val s = FastForwardSwitch()
        s.release(50) // nothing was pressed
        assertFalse(s.fast)
        s.press(100)
        s.press(200) // a keyboard key repeating, or a second finger
        s.release(250) // 150 ms after the first press: a tap
        assertTrue(s.switchedOn)
    }

    @Test
    fun resetForgetsEverything() {
        val s = FastForwardSwitch()
        s.press(0)
        s.release(10)
        s.press(500)
        s.reset()
        assertFalse(s.fast || s.switchedOn || s.held)
    }
}
