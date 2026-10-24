package com.tengen.gbaemu

import android.view.KeyEvent
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Test

class KeyBindingsTest {
    @After
    fun backToTheDefaults() = KeyBindings(MemoryStore()).use() // KeyMap is shared by every test

    @Test
    fun theDefaultsAreTheKeysOfMilestone4() {
        val k = KeyBindings(MemoryStore())
        for (b in KeyMap.bindings) {
            assertEquals(b.button, b.keyboard.keys.first(), k.key(b.bit, KeyBindings.KEYBOARD))
            assertEquals(b.button, b.gamepad.keys.first(), k.key(b.bit, KeyBindings.CONTROLLER))
        }
    }

    @Test
    fun aKeyAlreadyUsedMovesAndLeavesItsOldButtonEmpty() {
        val k = KeyBindings(MemoryStore())
        k.assign(KeyMap.B, KeyBindings.KEYBOARD, KeyEvent.KEYCODE_X) // X was A's
        assertEquals(KeyEvent.KEYCODE_X, k.key(KeyMap.B, KeyBindings.KEYBOARD))
        assertEquals(KeyBindings.NONE, k.key(KeyMap.A, KeyBindings.KEYBOARD))
        assertEquals(KeyEvent.KEYCODE_BUTTON_A, k.key(KeyMap.A, KeyBindings.CONTROLLER)) // the other column untouched
        k.assign(KeyMap.A, KeyBindings.CONTROLLER, KeyEvent.KEYCODE_BUTTON_Y) // a new key: nothing else changes
        assertEquals(KeyEvent.KEYCODE_BUTTON_Y, k.key(KeyMap.A, KeyBindings.CONTROLLER))
        assertEquals(KeyEvent.KEYCODE_BUTTON_B, k.key(KeyMap.B, KeyBindings.CONTROLLER))
    }

    @Test
    fun theKeysAreSavedResetAndUsedByKeyMap() {
        val store = MemoryStore()
        val k = KeyBindings(store)
        k.assign(KeyMap.START, KeyBindings.KEYBOARD, KeyEvent.KEYCODE_SPACE)
        k.assign(KeyMap.FAST, KeyBindings.CONTROLLER, KeyEvent.KEYCODE_BUTTON_Y)
        val again = KeyBindings(store)
        assertEquals(KeyEvent.KEYCODE_SPACE, again.key(KeyMap.START, KeyBindings.KEYBOARD))
        again.use()
        assertEquals(KeyMap.START, KeyMap.bitFor(KeyEvent.KEYCODE_SPACE))
        assertEquals(0, KeyMap.bitFor(KeyEvent.KEYCODE_ENTER)) // no longer Start
        assertEquals(KeyMap.FAST, KeyMap.bitFor(KeyEvent.KEYCODE_BUTTON_Y))
        assertEquals(0, KeyMap.bitFor(KeyEvent.KEYCODE_BUTTON_R2))
        again.reset()
        again.use()
        assertEquals(KeyEvent.KEYCODE_ENTER, KeyBindings(store).key(KeyMap.START, KeyBindings.KEYBOARD))
        assertEquals(KeyMap.START, KeyMap.bitFor(KeyEvent.KEYCODE_ENTER))
    }

    @Test
    fun onlyKeyboardKeysForFastForwardAndRewindGoThroughTheRepeatFilter() {
        val k = KeyBindings(MemoryStore())
        k.use()
        assertEquals(true, KeyMap.isKeyboardExtra(KeyEvent.KEYCODE_F))
        k.assign(KeyMap.FAST, KeyBindings.KEYBOARD, KeyEvent.KEYCODE_G)
        k.use()
        assertEquals(false, KeyMap.isKeyboardExtra(KeyEvent.KEYCODE_F))
        assertEquals(true, KeyMap.isKeyboardExtra(KeyEvent.KEYCODE_G))
        assertEquals(false, KeyMap.isKeyboardExtra(KeyEvent.KEYCODE_BUTTON_R2))
    }
}
