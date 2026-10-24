package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/** Buttons reach the game — even a tap shorter than a frame (the phone emulator sends letter keys that way). */
@RunWith(AndroidJUnit4::class)
class KeyTest {
    @After
    fun stopSession() = NativeBridge.stop()

    private fun startGame() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(300)
    }

    @Test
    fun aTapShorterThanAFrameReachesTheGame() {
        startGame()
        repeat(20) {
            NativeBridge.debugKeysSeen() // forget what came before
            NativeBridge.setKeys(KeyMap.A)
            NativeBridge.setKeys(0)
            Thread.sleep(50) // three frames
            assertEquals("tap ${it + 1}", KeyMap.A, NativeBridge.debugKeysSeen())
        }
    }

    @Test
    fun aHeldButtonIsSeenAndAReleasedOneIsNot() {
        startGame()
        NativeBridge.setKeys(KeyMap.UP)
        Thread.sleep(100)
        NativeBridge.debugKeysSeen()
        Thread.sleep(100)
        assertEquals("held", KeyMap.UP, NativeBridge.debugKeysSeen())
        NativeBridge.setKeys(0)
        Thread.sleep(100)
        NativeBridge.debugKeysSeen()
        Thread.sleep(100)
        assertEquals("released", 0, NativeBridge.debugKeysSeen())
    }
}
