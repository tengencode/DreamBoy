package com.tengen.gbaemu

import android.os.SystemClock
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith

/** The touch buttons, fed with made-up finger events: several fingers, sliding, diagonals, hiding. */
@RunWith(AndroidJUnit4::class)
class TouchTest {
    private val inst = InstrumentationRegistry.getInstrumentation()
    private lateinit var view: TouchControlsView
    private var mask = -1

    private fun makeView(width: Int, height: Int, sideways: Boolean) = inst.runOnMainSync {
        view = TouchControlsView(inst.targetContext)
        view.sideways = sideways
        view.onMask = { mask = it }
        view.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY))
        view.layout(0, 0, width, height)
    }

    private fun centre(bit: Int) = view.touchLayout!!.buttons.first { it.bit == bit }.let { it.cx to it.cy }

    /** A point on the D-pad: 70 % out from the centre at `degrees` (0 = right, 90 = up). */
    private fun dpad(degrees: Double): Pair<Float, Float> {
        val l = view.touchLayout!!
        val a = Math.toRadians(degrees)
        return (l.dpadX + 0.7f * l.dpadRadius * Math.cos(a).toFloat()) to (l.dpadY - 0.7f * l.dpadRadius * Math.sin(a).toFloat())
    }

    private fun send(action: Int, vararg fingers: Pair<Float, Float>) {
        val props = Array(fingers.size) { i ->
            MotionEvent.PointerProperties().apply { id = i; toolType = MotionEvent.TOOL_TYPE_FINGER }
        }
        val coords = Array(fingers.size) { i ->
            MotionEvent.PointerCoords().apply { x = fingers[i].first; y = fingers[i].second; pressure = 1f; size = 1f }
        }
        val now = SystemClock.uptimeMillis()
        val e = MotionEvent.obtain(now, now, action, fingers.size, props, coords, 0, 0, 1f, 1f, 0, 0,
            InputDevice.SOURCE_TOUCHSCREEN, 0)
        inst.runOnMainSync { view.dispatchTouchEvent(e) }
        e.recycle()
    }

    private fun pointer(action: Int, index: Int) = action or (index shl MotionEvent.ACTION_POINTER_INDEX_SHIFT)

    @Test
    fun oneFingerPressesAndReleasesA() {
        makeView(1080, 1700, sideways = false)
        send(MotionEvent.ACTION_DOWN, centre(KeyMap.A))
        assertEquals(KeyMap.A, mask)
        send(MotionEvent.ACTION_UP, centre(KeyMap.A))
        assertEquals(0, mask)
    }

    @Test
    fun twoFingersAAndADiagonal() {
        makeView(1080, 1700, sideways = false)
        val a = centre(KeyMap.A)
        val upRight = dpad(45.0)
        send(MotionEvent.ACTION_DOWN, a)
        send(pointer(MotionEvent.ACTION_POINTER_DOWN, 1), a, upRight)
        assertEquals(KeyMap.A or KeyMap.UP or KeyMap.RIGHT, mask)
        send(pointer(MotionEvent.ACTION_POINTER_UP, 0), a, upRight)
        assertEquals(KeyMap.UP or KeyMap.RIGHT, mask)
        send(MotionEvent.ACTION_UP, upRight)
        assertEquals(0, mask)
    }

    @Test
    fun slidingFromBToAMovesThePress() {
        makeView(1080, 1700, sideways = false)
        send(MotionEvent.ACTION_DOWN, centre(KeyMap.B))
        assertEquals(KeyMap.B, mask)
        send(MotionEvent.ACTION_MOVE, centre(KeyMap.A))
        assertEquals(KeyMap.A, mask)
        send(MotionEvent.ACTION_UP, centre(KeyMap.A))
        assertEquals(0, mask)
    }

    @Test
    fun hidingReleasesAndTheShowingTouchPressesNothing() {
        makeView(1080, 1700, sideways = false)
        var shown = false
        view.onShow = { shown = true }
        send(MotionEvent.ACTION_DOWN, centre(KeyMap.A))
        inst.runOnMainSync { view.hidden = true } // a keyboard key was pressed
        assertEquals(0, mask)
        send(MotionEvent.ACTION_UP, centre(KeyMap.A))
        send(MotionEvent.ACTION_DOWN, centre(KeyMap.A)) // shows the buttons...
        assertEquals(0, mask) // ...without pressing A
        assertFalse(view.hidden)
        assertEquals(true, shown)
        send(MotionEvent.ACTION_UP, centre(KeyMap.A))
        send(MotionEvent.ACTION_DOWN, centre(KeyMap.A))
        assertEquals(KeyMap.A, mask)
    }

    @Test
    fun sidewaysButtonsWorkToo() {
        makeView(2400, 1080, sideways = true)
        for (bit in listOf(KeyMap.A, KeyMap.B, KeyMap.L, KeyMap.R, KeyMap.START, KeyMap.SELECT)) {
            send(MotionEvent.ACTION_DOWN, centre(bit))
            assertEquals(bit, mask)
            send(MotionEvent.ACTION_UP, centre(bit))
        }
        send(MotionEvent.ACTION_DOWN, dpad(180.0))
        assertEquals(KeyMap.LEFT, mask)
        send(MotionEvent.ACTION_UP, dpad(180.0))
        assertEquals(0, mask)
    }
}
