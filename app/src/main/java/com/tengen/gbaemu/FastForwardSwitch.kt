package com.tengen.gbaemu

/**
 * The ⏩ button (or its key): fast as soon as it is pressed. Let go within [TAP_MS] and it was a tap, which flips
 * the switch (on if it was off, off if it was on); let go later and it was a hold, which ends at normal speed.
 * Pure (times given in ms), so it is tested on the PC.
 */
class FastForwardSwitch {
    /** Fast mode stays on after the button is let go (the button is drawn lit). */
    var switchedOn = false
        private set
    private var pressedAt = -1L

    val held get() = pressedAt >= 0
    val fast get() = held || switchedOn

    fun press(nowMs: Long) {
        if (pressedAt < 0) pressedAt = nowMs
    }

    fun release(nowMs: Long) {
        if (pressedAt < 0) return
        val tap = nowMs - pressedAt < TAP_MS
        pressedAt = -1
        switchedOn = tap && !switchedOn
    }

    fun reset() {
        pressedAt = -1
        switchedOn = false
    }

    companion object {
        const val TAP_MS = 300L
    }
}
