package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class KeyRepeatFilterTest {
    private val bit = KeyMap.FAST

    @Test
    fun aRealPressAndReleaseEndsSoonAfterTheRelease() {
        val f = KeyRepeatFilter()
        assertTrue(f.down(bit, 1000)) // a new hold
        val wait = f.up(bit, 1400)
        assertEquals(KeyRepeatFilter.GAP_MS, wait)
        assertTrue(f.due(1400 + wait - 1).isEmpty())
        assertEquals(listOf(bit to 1400L), f.due(1400 + wait)) // released at the time of the key-up
        assertFalse(f.held(bit))
    }

    @Test
    fun aSingleEmulatorTapIsAShortHoldThatEndsAtOnce() { // F tapped: the switch flips without waiting
        val f = KeyRepeatFilter()
        f.down(bit, 100)
        assertEquals(KeyRepeatFilter.GAP_MS, f.up(bit, 100))
        assertEquals(listOf(bit to 100L), f.due(100 + KeyRepeatFilter.GAP_MS))
    }

    @Test
    fun theEmulatorsRepeatStreamIsOneHold() { // after the PC's repeat delay: a tap every 33 ms
        val f = KeyRepeatFilter()
        assertTrue(f.down(bit, 500))
        var t = 500L
        while (t < 2000) {
            f.up(bit, t)
            t += 33
            assertTrue(f.due(t).isEmpty())
            assertFalse("the same hold", f.down(bit, t))
        }
        val wait = f.up(bit, t)
        assertEquals(listOf(bit to t), f.due(t + wait))
        assertTrue("the next press starts a new hold", f.down(bit, t + 1000))
    }

    @Test
    fun keysAreSeparateAndAStrayUpIsIgnored() {
        val f = KeyRepeatFilter()
        assertEquals(-1L, f.up(KeyMap.REWIND, 10))
        f.down(KeyMap.FAST, 0)
        f.down(KeyMap.REWIND, 0)
        f.up(KeyMap.REWIND, 1000)
        assertEquals(listOf(KeyMap.REWIND to 1000L), f.due(2000))
        assertTrue(f.held(KeyMap.FAST))
        f.clear()
        assertFalse(f.held(KeyMap.FAST))
    }
}
