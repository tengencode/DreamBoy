package com.tengen.gbaemu

import android.view.KeyEvent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class KeyMapTest {
    @Test
    fun everyMappedKeySetsItsButton() {
        val expected = mapOf(
            KeyEvent.KEYCODE_DPAD_UP to KeyMap.UP,
            KeyEvent.KEYCODE_DPAD_DOWN to KeyMap.DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT to KeyMap.LEFT,
            KeyEvent.KEYCODE_DPAD_RIGHT to KeyMap.RIGHT,
            KeyEvent.KEYCODE_X to KeyMap.A,
            KeyEvent.KEYCODE_BUTTON_A to KeyMap.A,
            KeyEvent.KEYCODE_Z to KeyMap.B,
            KeyEvent.KEYCODE_BUTTON_B to KeyMap.B,
            KeyEvent.KEYCODE_A to KeyMap.L,
            KeyEvent.KEYCODE_BUTTON_L1 to KeyMap.L,
            KeyEvent.KEYCODE_S to KeyMap.R,
            KeyEvent.KEYCODE_BUTTON_R1 to KeyMap.R,
            KeyEvent.KEYCODE_ENTER to KeyMap.START,
            KeyEvent.KEYCODE_BUTTON_START to KeyMap.START,
            KeyEvent.KEYCODE_DEL to KeyMap.SELECT,
            KeyEvent.KEYCODE_BUTTON_SELECT to KeyMap.SELECT,
        )
        for ((key, bit) in expected) assertEquals("key $key", bit, KeyMap.bitFor(key))
    }

    @Test
    fun buttonBitsMatchTheCore() { /* GBA_KEY_* in core/include/gba.h */
        assertEquals(listOf(1, 2, 4, 8, 16, 32, 64, 128, 256, 512),
            listOf(KeyMap.A, KeyMap.B, KeyMap.SELECT, KeyMap.START, KeyMap.RIGHT, KeyMap.LEFT, KeyMap.UP, KeyMap.DOWN,
                KeyMap.R, KeyMap.L))
    }

    @Test
    fun pressAndReleaseChangeOnlyThatButton() {
        var mask = KeyMap.update(0, KeyEvent.KEYCODE_X, true)
        mask = KeyMap.update(mask, KeyEvent.KEYCODE_DPAD_UP, true)
        assertEquals(KeyMap.A or KeyMap.UP, mask)
        mask = KeyMap.update(mask, KeyEvent.KEYCODE_X, false)
        assertEquals(KeyMap.UP, mask)
    }

    @Test
    fun unmappedKeysChangeNothing() {
        assertEquals(0, KeyMap.bitFor(KeyEvent.KEYCODE_Q))
        assertEquals(KeyMap.UP, KeyMap.update(KeyMap.UP, KeyEvent.KEYCODE_Q, true))
    }

    @Test
    fun theControlsTableCoversEveryButtonOnceWithNames() {
        assertEquals(4095, KeyMap.bindings.sumOf { it.bit }) // every bit exactly once: 10 GBA buttons, fast-forward, rewind
        assertEquals(12, KeyMap.bindings.size)
        for (b in KeyMap.bindings) {
            assertTrue(b.button, b.keyboard.isNotEmpty() && b.gamepad.isNotEmpty())
            for (code in b.keyboard.keys + b.gamepad.keys) assertTrue(b.button, KeyMap.bitFor(code) == b.bit)
        }
    }

    @Test
    fun keyboardAndTouchButtonsAddUp() {
        assertEquals(KeyMap.A or KeyMap.UP or KeyMap.RIGHT, KeyMap.merge(KeyMap.A, KeyMap.UP or KeyMap.RIGHT))
        assertEquals(KeyMap.B, KeyMap.merge(KeyMap.B, KeyMap.B))
        assertEquals(0, KeyMap.merge(0, 0))
    }
}
