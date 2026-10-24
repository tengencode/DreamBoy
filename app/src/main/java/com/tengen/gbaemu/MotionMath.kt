package com.tengen.gbaemu

import android.view.KeyEvent
import kotlin.math.PI
import kotlin.math.log10
import kotlin.math.roundToInt

/**
 * Phone sensors and keys → the values the cartridge extras take (pure maths, tested on the PC):
 * tilt x/y and turn ±32767, light 0..255. Also the rumble on/off decision.
 */
object MotionMath {
    const val FULL = 32767
    const val KEY_TILT = 24000 // I/J/K/L held (as in the PC viewer)
    const val KEY_TURN = 16000 // Q/E held
    private const val G = 9.80665f
    private const val FULL_TURN_RATE = (4 * PI).toFloat() // rad/s: two turns per second = full turn

    /**
     * Gravity from the accelerometer (m/s², the device's natural axes: x right, y up the screen) → tilt, for the screen
     * turned by [rotation] quarter turns (Surface.ROTATION_0..270). Lowering the screen's right edge gives +x, lowering
     * its top edge −y; flat = (0, 0); 1 g along an edge = full tilt — like the cartridge's sensor, which measures gravity.
     */
    fun tilt(ax: Float, ay: Float, rotation: Int): Pair<Int, Int> {
        val (sx, sy) = when (rotation and 3) {
            0 -> ax to ay
            1 -> -ay to ax
            2 -> -ax to -ay
            else -> ay to -ax
        }
        return clamp(-sx / G * FULL) to clamp(sy / G * FULL)
    }

    /** The gyroscope's z rate (rad/s, counter-clockwise +) → turn: clockwise +, two turns per second = full. */
    fun turn(wz: Float): Int = clamp(-wz / FULL_TURN_RATE * FULL)

    /** Lux → light: 0 at 10 lux or less (a dark room), 255 at 100,000 lux or more (direct sun), log scale between. */
    fun light(lux: Float): Int =
        if (lux <= 10f) 0 else (255 * log10(lux / 10f) / 4f).roundToInt().coerceIn(0, 255)

    /** One axis: a held key replaces the sensor (both held cancel out to 0). */
    fun withKeys(sensor: Int, minusHeld: Boolean, plusHeld: Boolean, keyValue: Int): Int =
        if (minusHeld || plusHeld) (if (plusHeld) keyValue else 0) - (if (minusHeld) keyValue else 0) else sensor

    // Motion keys (bits), as in the PC viewer: I/K tilt up/down (−y/+y), J/L tilt left/right (−x/+x), Q/E turn −/+.
    const val KEY_I = 1
    const val KEY_J = 2
    const val KEY_K = 4
    const val KEY_L = 8
    const val KEY_Q = 16
    const val KEY_E = 32

    fun motionKey(keyCode: Int): Int = when (keyCode) {
        KeyEvent.KEYCODE_I -> KEY_I
        KeyEvent.KEYCODE_J -> KEY_J
        KeyEvent.KEYCODE_K -> KEY_K
        KeyEvent.KEYCODE_L -> KEY_L
        KeyEvent.KEYCODE_Q -> KEY_Q
        KeyEvent.KEYCODE_E -> KEY_E
        else -> 0
    }

    private fun clamp(v: Float): Int = v.roundToInt().coerceIn(-FULL, FULL)

    /** Rumble: act only when the motor changes. */
    enum class RumbleAction { START, STOP, NOTHING }

    class RumbleSwitch {
        var on = false
            private set

        fun next(active: Boolean): RumbleAction = when {
            active && !on -> RumbleAction.START.also { on = true }
            !active && on -> RumbleAction.STOP.also { on = false }
            else -> RumbleAction.NOTHING
        }

        fun reset() {
            on = false
        }
    }
}
