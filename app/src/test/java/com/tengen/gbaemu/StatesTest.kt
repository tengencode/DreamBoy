package com.tengen.gbaemu

import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class StatesTest {
    @Test
    fun filesPerGameAndSlot() {
        val s = States(File("files"), "1a2b3c4d")
        assertEquals(File("files/states/1a2b3c4d/slot3.state"), s.state(3))
        assertEquals(File("files/states/1a2b3c4d/slot10.png"), s.picture(10))
        assertEquals(File("files/states/1a2b3c4d/auto.state"), s.state(States.AUTO))
        assertEquals(File("files/states/1a2b3c4d/auto.png"), s.picture(States.AUTO))
        assertEquals(File("files/states/1a2b3c4d/quick.state"), s.state(States.QUICK))
        assertEquals(File("files/states/1a2b3c4d/quick.png"), s.picture(States.QUICK))
    }

    @Test
    fun picturesUseTheSameColoursAsTheScreen() {
        val px = shortArrayOf(0x0000, 0x7FFF, 0x001F, 0x03E0, 0x7C00, 0x0010)
        assertArrayEquals(
            intArrayOf(
                0xFF000000.toInt(), 0xFFFFFFFF.toInt(), // black, white
                0xFFFF0000.toInt(), 0xFF00FF00.toInt(), 0xFF0000FF.toInt(), // red, green, blue
                0xFF840000.toInt(), // red 16 of 31: (16 << 3) | (16 >> 2) = 0x84
            ),
            States.argbOf(px),
        )
    }
}
