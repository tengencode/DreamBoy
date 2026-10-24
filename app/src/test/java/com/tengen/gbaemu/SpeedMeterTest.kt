package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class SpeedMeterTest {
    private val second = 1_000_000_000L

    @Test
    fun nothingUntilASecondHasBeenMeasured() {
        val m = SpeedMeter()
        assertNull(m.sample(0, 100))
        assertNull(m.sample(second / 2, 130))
        assertEquals("60 fps · 1.0×", m.sample(second, 160))
    }

    @Test
    fun fastForwardShowsMoreFramesAndASpeedUp() {
        val m = SpeedMeter()
        m.sample(0, 0)
        assertEquals("239 fps · 4.0×", m.sample(second, 239))
        assertEquals("239 fps · 4.0×", m.sample(second + second / 3, 300)) // under a second since: unchanged
        assertEquals("30 fps · 0.5×", m.sample(2 * second, 269))
    }

    @Test
    fun aNewGameStartsTheMeasureAgain() {
        val m = SpeedMeter()
        m.sample(0, 5000)
        assertEquals("60 fps · 1.0×", m.sample(second, 5060))
        assertEquals("60 fps · 1.0×", m.sample(2 * second, 10)) // the count restarted: the old label stays
        assertEquals("120 fps · 2.0×", m.sample(3 * second, 130))
    }
}
