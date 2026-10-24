package com.tengen.gbaemu

import android.content.Context
import android.os.Build
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.Choreographer

/** The cartridge's rumble motor → the phone's vibrator, checked once per display frame while running. */
class Rumble(context: Context) : Choreographer.FrameCallback {
    private val vibrator: Vibrator? = if (Build.VERSION.SDK_INT >= 31) {
        context.getSystemService(VibratorManager::class.java)?.defaultVibrator
    } else {
        @Suppress("DEPRECATION")
        context.getSystemService(Vibrator::class.java)
    }
    private val switch = MotionMath.RumbleSwitch()
    private var running = false
    private var startedMs = 0L

    fun start() {
        if (running || vibrator?.hasVibrator() != true) return
        running = true
        Choreographer.getInstance().postFrameCallback(this)
    }

    /** Pause, quit, the app in the background: the motor stops with the game. */
    fun stop() {
        if (!running) return
        running = false
        Choreographer.getInstance().removeFrameCallback(this)
        if (switch.on) vibrator?.cancel()
        switch.reset()
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!running) return
        when (switch.next(NativeBridge.rumbleActive())) {
            MotionMath.RumbleAction.START -> buzz()
            MotionMath.RumbleAction.STOP -> vibrator?.cancel()
            MotionMath.RumbleAction.NOTHING -> if (switch.on && SystemClock.elapsedRealtime() - startedMs > LONG_MS - 1000) buzz()
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    /** A long buzz, renewed while the motor keeps running, cancelled when it stops. */
    private fun buzz() {
        vibrator?.vibrate(VibrationEffect.createOneShot(LONG_MS, VibrationEffect.DEFAULT_AMPLITUDE))
        startedMs = SystemClock.elapsedRealtime()
    }

    private companion object {
        const val LONG_MS = 5000L
    }
}
