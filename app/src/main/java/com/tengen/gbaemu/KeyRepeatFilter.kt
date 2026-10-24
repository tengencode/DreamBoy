package com.tengen.gbaemu

/**
 * Held keyboard keys for ⏩ and ⟲, as the phone emulator's window sends them (checked with getevent, 2026-09-27):
 * a held letter arrives as a 0.1 ms tap, then — after the PC's key-repeat delay (~500 ms) — a tap every ~33 ms,
 * with no key-up when it is let go; the PC repeats only the last key pressed, so pressing an arrow stops the stream.
 * A key-up therefore ends the hold only if no key-down follows within [GAP_MS], and it ends at the key-up's time:
 * a lone tap of F flips fast mode, R held on its own rewinds smoothly. Real keyboards (key-downs repeat, one key-up)
 * only lose [GAP_MS]. Pure: times in ms.
 */
class KeyRepeatFilter {
    private val holdStart = HashMap<Int, Long>() // bit → when this hold began
    private val pending = HashMap<Int, Pair<Long, Long>>() // bit → (release due at, key-up time)

    /** A key-down; true if it starts a new hold (false: the same hold goes on). */
    fun down(bit: Int, now: Long): Boolean {
        pending.remove(bit)
        return holdStart.putIfAbsent(bit, now) == null
    }

    /** A key-up: how long to wait before calling [due] (-1: the key was not held). */
    fun up(bit: Int, now: Long): Long {
        if (bit !in holdStart) return -1
        pending[bit] = (now + GAP_MS) to now
        return GAP_MS
    }

    /** Holds that have ended by [now]: (bit, key-up time) each. */
    fun due(now: Long): List<Pair<Int, Long>> {
        val ended = pending.filter { it.value.first <= now }.map { it.key to it.value.second }
        for ((bit, _) in ended) {
            pending.remove(bit)
            holdStart.remove(bit)
        }
        return ended
    }

    fun held(bit: Int) = bit in holdStart

    fun clear() {
        holdStart.clear()
        pending.clear()
    }

    companion object {
        const val GAP_MS = 60L // longer than the ~33 ms between repeats
    }
}
