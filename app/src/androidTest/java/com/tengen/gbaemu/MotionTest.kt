package com.tengen.gbaemu

import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Cartridge extras on the phone: which ones a cartridge has, sensor values reaching the core, the rumble motor. */
@RunWith(AndroidJUnit4::class)
class MotionTest {
    @After
    fun stopSession() = NativeBridge.stop()

    private fun load(rom: String) = assertEquals(0, NativeBridge.loadRom(TestRoms.rom(rom)))

    @Test
    fun cartridgesReportTheirExtras() {
        load("../own_roms/hw_gyro.gba")
        assertEquals(NativeBridge.HW_GYRO or NativeBridge.HW_RUMBLE, NativeBridge.cartHardware())
        load("../own_roms/hw_tilt.gba")
        assertEquals(NativeBridge.HW_TILT, NativeBridge.cartHardware())
        load("../own_roms/hw_solar.gba")
        assertEquals(NativeBridge.HW_RTC or NativeBridge.HW_SOLAR, NativeBridge.cartHardware())
        load("tonc/m7_demo.gba")
        assertEquals(0, NativeBridge.cartHardware())
    }

    /** What the core last saw, once it is [expected] or after 2 s (the first frames wait for the sound to start). */
    private fun seenSoon(expected: IntArray): IntArray {
        val until = SystemClock.elapsedRealtime() + 2000
        var seen = NativeBridge.debugMotionSeen()
        while (!seen.contentEquals(expected) && SystemClock.elapsedRealtime() < until) {
            Thread.sleep(5)
            seen = NativeBridge.debugMotionSeen()
        }
        return seen
    }

    @Test
    fun sensorValuesReachTheCoreWithinAFrame() {
        load("../own_roms/hw_tilt.gba")
        NativeBridge.start()
        NativeBridge.setMotion(12000, -5000, 16000)
        NativeBridge.setLight(200)
        assertArrayEquals(intArrayOf(12000, -5000, 16000, 200), seenSoon(intArrayOf(12000, -5000, 16000, 200)))
        NativeBridge.setMotion(99999, -99999, 0) // out of range: clamped
        NativeBridge.setLight(300)
        Thread.sleep(100) // the game is running now: one frame is 17 ms
        assertArrayEquals(intArrayOf(32767, -32767, 0, 255), NativeBridge.debugMotionSeen())
    }

    @Test
    fun theRumbleMotorGoesOnAndOffHalfTheTime() {
        load("../own_roms/hw_gyro.gba") // on 30 frames, off 30 frames
        NativeBridge.start()
        var on = 0
        var off = 0
        val until = SystemClock.elapsedRealtime() + 2000
        while (SystemClock.elapsedRealtime() < until) {
            if (NativeBridge.rumbleActive()) on++ else off++
            Thread.sleep(5)
        }
        val share = on.toDouble() / (on + off)
        assertTrue("motor on in $on of ${on + off} samples", share in 0.35..0.65)
    }

    @Test
    fun noRumbleWithoutAMotor() {
        load("tonc/m7_demo.gba")
        NativeBridge.start()
        val until = SystemClock.elapsedRealtime() + 500
        while (SystemClock.elapsedRealtime() < until) {
            assertEquals(false, NativeBridge.rumbleActive())
            Thread.sleep(20)
        }
    }
}
