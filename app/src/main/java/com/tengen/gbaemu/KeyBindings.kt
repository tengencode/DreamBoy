package com.tengen.gbaemu

import android.view.KeyEvent

/**
 * Which keyboard key and which controller button press each GBA button (and ⏩ ⟲): one of each, stored in an
 * [IntStore] (the "keys" preferences) as `kbd.<bit>` / `pad.<bit>` = the Android key code (0 = none). Nothing
 * stored = the defaults ([KeyMap.bindings]). [use] makes them the ones [KeyMap] reads.
 */
class KeyBindings(private val store: IntStore) {
    /** The key for [bit] on [device] ([KEYBOARD] or [CONTROLLER]); [NONE] if it has none. */
    fun key(bit: Int, device: Int): Int = store.get(name(bit, device)) ?: default(bit, device)

    /** [keyCode] now presses [bit]; if another button had it, that one is left with none. */
    fun assign(bit: Int, device: Int, keyCode: Int) {
        for (b in KeyMap.bindings) {
            if (b.bit != bit && key(b.bit, device) == keyCode) store.put(name(b.bit, device), NONE)
        }
        store.put(name(bit, device), keyCode)
    }

    /** Back to the defaults. */
    fun reset() {
        for (b in KeyMap.bindings) for (device in listOf(KEYBOARD, CONTROLLER)) store.put(name(b.bit, device), null)
    }

    /** From now on key events are read with these keys. */
    fun use() {
        KeyMap.keyboard = map(KEYBOARD)
        KeyMap.gamepad = map(CONTROLLER)
    }

    private fun map(device: Int): Map<Int, Int> =
        KeyMap.bindings.mapNotNull { b -> key(b.bit, device).takeIf { it != NONE }?.let { it to b.bit } }.toMap()

    private fun name(bit: Int, device: Int) = (if (device == KEYBOARD) "kbd." else "pad.") + bit

    private fun default(bit: Int, device: Int): Int {
        val b = KeyMap.bindings.first { it.bit == bit }
        return (if (device == KEYBOARD) b.keyboard else b.gamepad).keys.first()
    }

    companion object {
        const val KEYBOARD = 0
        const val CONTROLLER = 1
        const val NONE = 0 // KeyEvent.KEYCODE_UNKNOWN

        /** A key's name to show: the default table's name (X, Enter, ↑, L1 …) or Android's (Space, Button Y …). */
        fun keyName(keyCode: Int): String {
            if (keyCode == NONE) return "—"
            for (b in KeyMap.bindings) (b.keyboard[keyCode] ?: b.gamepad[keyCode])?.let { return it }
            val raw = KeyEvent.keyCodeToString(keyCode).removePrefix("KEYCODE_")
            return raw.split('_').joinToString(" ") { w -> w.lowercase().replaceFirstChar { it.uppercase() } }
        }
    }
}
