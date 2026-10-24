package com.tengen.gbaemu

import android.app.Activity
import android.content.pm.ApplicationInfo
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.SystemClock
import android.util.Log
import androidx.core.content.ContextCompat

/**
 * The cartridge's sensors from the phone: the accelerometer (tilt), the gyroscope (turn) and the light sensor (solar),
 * each only when the cartridge has it ([hardware] = NativeBridge.HW_* bits); the I/J/K/L and Q/E keys override their
 * axis while held; [manualLight] (the ☀ slider) overrides the light sensor. Every change goes to the core at once.
 */
class MotionInput(private val activity: Activity, private val hardware: Int, private val onMissing: (String) -> Unit) :
    SensorEventListener {
    private val sensors = activity.getSystemService(SensorManager::class.java)
    private val debug = (activity.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
    private var ax = 0f
    private var ay = 0f
    private var wz = 0f
    private var lux = 0f
    private var keys = 0
    private var missingTold = false
    private var lastLog = ""
    private var lastLogMs = 0L
    private var started = false

    /** null = Auto (the light sensor); 0..255 = set by hand. */
    var manualLight: Int? = null
        set(value) {
            field = value
            push()
        }

    fun start() {
        if (started) return
        started = true
        val wanted = buildList {
            if (hardware and NativeBridge.HW_TILT != 0) add(Sensor.TYPE_ACCELEROMETER to "motion (tilt)")
            if (hardware and NativeBridge.HW_GYRO != 0) add(Sensor.TYPE_GYROSCOPE to "gyroscope (turn)")
            if (hardware and NativeBridge.HW_SOLAR != 0) add(Sensor.TYPE_LIGHT to "light")
        }
        val missing = mutableListOf<String>()
        for ((type, name) in wanted) {
            val s = sensors?.getDefaultSensor(type)
            if (s == null) missing += name else sensors.registerListener(this, s, SensorManager.SENSOR_DELAY_GAME)
        }
        if (missing.isNotEmpty() && !missingTold) {
            missingTold = true
            onMissing("This phone has no ${missing.joinToString(" or ")} sensor: use the keys or the light slider.")
        }
        push()
    }

    fun stop() {
        if (!started) return
        started = false
        sensors?.unregisterListener(this)
    }

    /** A motion key (MotionMath.KEY_*) went down or up. */
    fun setKey(bit: Int, down: Boolean) {
        keys = if (down) keys or bit else keys and bit.inv()
        push()
    }

    override fun onSensorChanged(event: SensorEvent) {
        when (event.sensor.type) {
            Sensor.TYPE_ACCELEROMETER -> {
                ax = event.values[0]
                ay = event.values[1]
            }
            Sensor.TYPE_GYROSCOPE -> wz = event.values[2]
            Sensor.TYPE_LIGHT -> lux = event.values[0]
        }
        push()
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}

    private fun push() {
        val rotation = ContextCompat.getDisplayOrDefault(activity).rotation
        val (sx, sy) = MotionMath.tilt(ax, ay, rotation)
        val x = MotionMath.withKeys(sx, keys and MotionMath.KEY_J != 0, keys and MotionMath.KEY_L != 0, MotionMath.KEY_TILT)
        val y = MotionMath.withKeys(sy, keys and MotionMath.KEY_I != 0, keys and MotionMath.KEY_K != 0, MotionMath.KEY_TILT)
        val turn = MotionMath.withKeys(MotionMath.turn(wz), keys and MotionMath.KEY_Q != 0, keys and MotionMath.KEY_E != 0,
            MotionMath.KEY_TURN)
        val light = manualLight ?: MotionMath.light(lux)
        NativeBridge.setMotion(x, y, turn)
        NativeBridge.setLight(light)
        if (debug) { // for checking with the phone emulator's virtual sensors: at most once a second, on changes
            val line = "motion: tilt $x $y turn $turn light $light${if (manualLight != null) " (manual)" else ""}"
            val now = SystemClock.elapsedRealtime()
            if (line != lastLog && now - lastLogMs >= 1000) {
                Log.i("gbaemu", line)
                lastLog = line
                lastLogMs = now
            }
        }
    }
}
