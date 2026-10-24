package com.tengen.gbaemu

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * A Delta skin (9) over the whole screen: its picture placed by [DeltaLayout] (made on a background thread by
 * [SkinPicture]; nothing of the skin is drawn until it is ready — [onPictureFailed] if it can't be), an opaque skin's
 * leftover strips in the colours of the picture's edges, and the items under a finger darkened. Fingers press the GBA
 * buttons ([onMask], as TouchControlsView) and fire actions ([onAction]), with a vibration tick unless [buzz] is off.
 * A translucent skin is see-through by [look] (Faint 40 %, Normal 70 % — Delta's own — Strong 100 %). While [hidden]
 * (a keyboard or gamepad in use) nothing is drawn; the next touch shows it again and presses nothing.
 */
class DeltaSkinView(context: Context) : View(context) {
    var skin: SkinFiles.Imported? = null
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }
    var sideways = false
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }
    var look = Settings.LOOK_NORMAL
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }
    var hidden = false
        set(value) {
            if (field == value) return
            field = value
            if (value) release()
            invalidate()
        }
    var buzz = true
    var onMask: (Int) -> Unit = {}
    var onAction: (DeltaSkin.Action) -> Unit = {}
    var onShow: () -> Unit = {}
    var onPictureFailed: () -> Unit = {}

    private var layout: DeltaLayout? = null
    private var touch: DeltaTouch? = null
    private var picture: Bitmap? = null
    private var edges: IntArray? = null // the picture's edge colours: DeltaLayout.TOP, BOTTOM, LEFT, RIGHT
    private var generation = 0 // a picture made for an older size or skin is dropped
    private var mask = 0
    private var ignoreUntilUp = false // the touch that showed the skin presses nothing
    private val paint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val fill = Paint()
    private val shade = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = SHADE }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) = relayout()

    private fun relayout() {
        release()
        picture = null
        edges = null
        val s = skin
        val face = s?.skin?.face(sideways)
        val l = if (face != null && width > 0 && height > 0) DeltaLayout(face, width.toFloat(), height.toFloat(), sideways) else null
        layout = l
        touch = l?.let(::DeltaTouch)
        val gen = ++generation
        if (s != null && l != null) {
            val w = (l.picture.right - l.picture.left).roundToInt()
            val h = (l.picture.bottom - l.picture.top).roundToInt()
            val side = sideways
            Thread {
                val bmp = SkinPicture.kept(s.dir, l.face.image, side, w, h)
                post {
                    if (gen != generation) return@post
                    if (bmp == null) {
                        onPictureFailed()
                    } else {
                        picture = bmp
                        edges = edgeColours(bmp)
                        invalidate()
                    }
                }
            }.start()
        }
        invalidate()
    }

    /** Nothing held (a new layout, or hidden). */
    private fun release() {
        touch?.update(emptyMap())
        setMask(0)
    }

    @SuppressLint("ClickableViewAccessibility") // game buttons, not clicks
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val t = touch ?: return false
        val action = event.actionMasked
        if (hidden) {
            if (action == MotionEvent.ACTION_DOWN) {
                hidden = false
                ignoreUntilUp = true
                onShow()
            }
            return true
        }
        if (ignoreUntilUp) {
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) ignoreUntilUp = false
            return true
        }
        val fingers = HashMap<Int, Pair<Float, Float>>()
        if (action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
            val lifted = if (action == MotionEvent.ACTION_POINTER_UP) event.actionIndex else -1
            for (i in 0 until event.pointerCount) {
                if (i != lifted) fingers[event.getPointerId(i)] = event.getX(i) to event.getY(i)
            }
        }
        val before = t.held.toSet()
        val fire = t.update(fingers)
        setMask(t.mask)
        if (t.held.toSet() != before) invalidate()
        if (fire.isNotEmpty() && buzz) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        fire.forEach(onAction)
        return true
    }

    private fun setMask(pressed: Int) {
        if (pressed == mask) return
        if (buzz && pressed and mask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        mask = pressed
        onMask(pressed)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        val l = layout ?: return
        val bmp = picture ?: return
        if (hidden) return
        if (!l.face.translucent) edges?.let { e ->
            for ((side, b) in l.leftovers()) {
                fill.color = e[side]
                canvas.drawRect(b.left, b.top, b.right, b.bottom, fill)
            }
        }
        paint.alpha = if (!l.face.translucent) 255 else when (look) {
            Settings.LOOK_FAINT -> 102
            Settings.LOOK_STRONG -> 255
            else -> 179
        }
        canvas.drawBitmap(bmp, Rect(0, 0, bmp.width, bmp.height),
            RectF(l.picture.left, l.picture.top, l.picture.right, l.picture.bottom), paint)
        for (item in touch?.held?.toSet() ?: emptySet()) {
            val b = l.box(item.frame)
            val w = b.right - b.left
            val h = b.bottom - b.top
            if (item.dpad) { // the pressed cells
                val cw = w / 3
                val ch = h / 3
                val r = cw * 0.15f
                fun cell(column: Int, row: Int) = canvas.drawRoundRect(b.left + column * cw, b.top + row * ch,
                    b.left + (column + 1) * cw, b.top + (row + 1) * ch, r, r, shade)
                if (mask and KeyMap.UP != 0) cell(1, 0)
                if (mask and KeyMap.DOWN != 0) cell(1, 2)
                if (mask and KeyMap.LEFT != 0) cell(0, 1)
                if (mask and KeyMap.RIGHT != 0) cell(2, 1)
            } else if (DeltaLayout.round(item.frame)) {
                canvas.drawOval(b.left, b.top, b.right, b.bottom, shade)
            } else {
                val r = min(w, h) / 2
                canvas.drawRoundRect(b.left, b.top, b.right, b.bottom, r, r, shade)
            }
        }
    }

    /** The middle pixel of each edge of the picture, opaque. */
    private fun edgeColours(b: Bitmap) = intArrayOf(
        b.getPixel(b.width / 2, 0), b.getPixel(b.width / 2, b.height - 1),
        b.getPixel(0, b.height / 2), b.getPixel(b.width - 1, b.height / 2),
    ).map { it or (0xFF shl 24) }.toIntArray()

    private companion object {
        const val SHADE = 0x40000000 // black at 25 %
    }
}
