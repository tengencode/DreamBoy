package com.tengen.gbaemu

import android.view.KeyEvent

/**
 * Keyboard and gamepad keys → GBA buttons (the same bits as GBA_KEY_* in core/include/gba.h), plus the app's own
 * fast-forward and rewind buttons above them (never sent to the game: see [GBA_BUTTONS]). [bindings] is the default
 * table; the keys in use are [keyboard] and [gamepad], which KeyBindings.use sets (Settings → Keyboard & controller).
 */
object KeyMap {
    const val A = 1 shl 0
    const val B = 1 shl 1
    const val SELECT = 1 shl 2
    const val START = 1 shl 3
    const val RIGHT = 1 shl 4
    const val LEFT = 1 shl 5
    const val UP = 1 shl 6
    const val DOWN = 1 shl 7
    const val R = 1 shl 8
    const val L = 1 shl 9
    const val FAST = 1 shl 10 // ⏩ fast-forward: tap or hold
    const val REWIND = 1 shl 11 // ⟲ rewind: hold
    const val GBA_BUTTONS = 0x3FF

    /** One GBA button and what presses it: keyboard keys and gamepad buttons (key code → name to show). */
    class Binding(val bit: Int, val button: String, val keyboard: Map<Int, String>, val gamepad: Map<Int, String>)

    /** The default map (and the buttons' names). */
    val bindings = listOf(
        Binding(A, "A", mapOf(KeyEvent.KEYCODE_X to "X"), mapOf(KeyEvent.KEYCODE_BUTTON_A to "A")),
        Binding(B, "B", mapOf(KeyEvent.KEYCODE_Z to "Z"), mapOf(KeyEvent.KEYCODE_BUTTON_B to "B")),
        Binding(L, "L", mapOf(KeyEvent.KEYCODE_A to "A"), mapOf(KeyEvent.KEYCODE_BUTTON_L1 to "L1")),
        Binding(R, "R", mapOf(KeyEvent.KEYCODE_S to "S"), mapOf(KeyEvent.KEYCODE_BUTTON_R1 to "R1")),
        Binding(START, "Start", mapOf(KeyEvent.KEYCODE_ENTER to "Enter"), mapOf(KeyEvent.KEYCODE_BUTTON_START to "Start")),
        Binding(SELECT, "Select", mapOf(KeyEvent.KEYCODE_DEL to "Backspace"), mapOf(KeyEvent.KEYCODE_BUTTON_SELECT to "Select")),
        Binding(UP, "Up", mapOf(KeyEvent.KEYCODE_DPAD_UP to "↑"), mapOf(KeyEvent.KEYCODE_DPAD_UP to "D-pad ↑")),
        Binding(DOWN, "Down", mapOf(KeyEvent.KEYCODE_DPAD_DOWN to "↓"), mapOf(KeyEvent.KEYCODE_DPAD_DOWN to "D-pad ↓")),
        Binding(LEFT, "Left", mapOf(KeyEvent.KEYCODE_DPAD_LEFT to "←"), mapOf(KeyEvent.KEYCODE_DPAD_LEFT to "D-pad ←")),
        Binding(RIGHT, "Right", mapOf(KeyEvent.KEYCODE_DPAD_RIGHT to "→"), mapOf(KeyEvent.KEYCODE_DPAD_RIGHT to "D-pad →")),
        Binding(FAST, "Fast-forward", mapOf(KeyEvent.KEYCODE_F to "F"), mapOf(KeyEvent.KEYCODE_BUTTON_R2 to "R2")),
        Binding(REWIND, "Rewind", mapOf(KeyEvent.KEYCODE_R to "R"), mapOf(KeyEvent.KEYCODE_BUTTON_L2 to "L2")),
    )

    /** The keys in use: key code → button bit (the defaults until KeyBindings.use). */
    @Volatile
    var keyboard: Map<Int, Int> = bindings.associate { it.keyboard.keys.first() to it.bit }

    @Volatile
    var gamepad: Map<Int, Int> = bindings.associate { it.gamepad.keys.first() to it.bit }

    /** A keyboard key (not a gamepad button) for ⏩ or ⟲: those go through KeyRepeatFilter. */
    fun isKeyboardExtra(keyCode: Int): Boolean = keyboard[keyCode].let { it == FAST || it == REWIND }

    /** The GBA button for an Android key code, or 0 if the key is not mapped. */
    fun bitFor(keyCode: Int): Int = keyboard[keyCode] ?: gamepad[keyCode] ?: 0

    /** The held-buttons mask after a key goes down or up. */
    fun update(mask: Int, keyCode: Int, down: Boolean): Int {
        val bit = bitFor(keyCode)
        return if (down) mask or bit else mask and bit.inv()
    }

    /** What the game gets: keyboard/gamepad buttons and touch buttons together. */
    fun merge(keyboardMask: Int, touchMask: Int): Int = keyboardMask or touchMask
}
