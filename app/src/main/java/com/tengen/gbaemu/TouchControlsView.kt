package com.tengen.gbaemu

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Rect
import android.graphics.RectF
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * The on-screen GBA buttons over the whole screen, placed by [TouchLayout] (the default layout, or the user's [edits]).
 * Every finger is followed; the pressed buttons go to [onMask] whenever they change, each newly pressed one with a
 * vibration tick (Android's own touch feedback, so the phone's setting applies) unless [buzz] is off. The colours are
 * the [skin]'s (8b): buttons over the game picture see-through ([look]: Settings.LOOK_*), the others solid with the
 * skin's rim. While [hidden] (a keyboard or gamepad is in use) nothing is drawn; the next touch shows the buttons again.
 *
 * [editing] (the layout editor): nothing is pressed; a finger picks the item under it ([onSelect]) and drags it
 * ([onEdit] with the new edits); the picture is drawn too ([picture], or a plain block) and the picked one outlined.
 */
class TouchControlsView(context: Context) : View(context) {
    /** Which default layout: sideways (buttons over the side bars) or upright (the area below the picture). */
    var sideways = false
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }

    /** The user's layout (6c): items moved and sized; empty = the default. */
    var edits: Map<LayoutItem, Place> = emptyMap()
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }

    var hidden = false
        set(value) {
            if (field == value) return
            field = value
            if (value) setMask(0)
            invalidate()
        }

    /** Fast mode switched on: ⏩ is drawn lit. */
    var fastOn = false
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }

    var look = Settings.LOOK_NORMAL
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }

    /** The colours (8b); the body round the picture is drawn behind this view (the game screen, OpenGL). */
    var skin: Skin = Skins.PLAIN
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }

    /** The picture's size mode (Settings.SIZE_*): where the game is drawn, so which buttons are over it. */
    var fitMode = Settings.SIZE_FIT
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }

    /** The game's console: the picture's shape, and whether L and R are there. */
    var console = Console.GBA
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }

    var buzz = true

    var onMask: (Int) -> Unit = {}
    var onShow: () -> Unit = {}

    var editing = false
    var selected: LayoutItem? = null
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }
    var picture: Bitmap? = null // the editor: the paused game's picture
        set(value) {
            field = value
            invalidate()
        }
    var onSelect: (LayoutItem?) -> Unit = {}
    var onEdit: (Map<LayoutItem, Place>) -> Unit = {}

    var touchLayout: TouchLayout? = null
        private set

    private var mask = 0
    private var ignoreUntilUp = false // the touch that showed the buttons presses nothing
    private var dragStart: Place? = null // the editor: where the dragged item was when the finger came down
    private var downX = 0f
    private var downY = 0f

    private val density = resources.displayMetrics.density
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }
    private val outline = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = AccentColors.dark(AccentColors.DEFAULT_SEED).color // the editor's outline: set from the theme's accent
        strokeWidth = 2 * resources.displayMetrics.density
        pathEffect = DashPathEffect(floatArrayOf(10f, 8f), 0f)
    }
    /** The layout editor's selection outline: the app's accent (12). */
    var selectColor: Int
        get() = outline.color
        set(v) {
            if (outline.color != v) { outline.color = v; invalidate() }
        }
    private val rim = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1.5f * resources.displayMetrics.density
    }

    private val dpadShape = Path() // the cross as one shape, so see-through arms do not overlap
    private val fastShape = Path()

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) = relayout()

    private fun relayout() {
        val l = if (width > 0 && height > 0) TouchLayout(width.toFloat(), height.toFloat(), density, sideways, edits, fitMode, console) else null
        touchLayout = l
        dpadShape.reset()
        if (l != null) {
            val r = l.dpadRadius
            val a = r * ARM
            val across = Path().apply { addRoundRect(l.dpadX - r, l.dpadY - a, l.dpadX + r, l.dpadY + a, a / 3, a / 3, Path.Direction.CW) }
            val down = Path().apply { addRoundRect(l.dpadX - a, l.dpadY - r, l.dpadX + a, l.dpadY + r, a / 3, a / 3, Path.Direction.CW) }
            dpadShape.op(across, down, Path.Op.UNION)
        }
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility") // game buttons, not clicks
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val layout = touchLayout ?: return false
        val action = event.actionMasked
        if (editing) return edit(layout, event)
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
        var pressed = 0
        if (action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
            val lifted = if (action == MotionEvent.ACTION_POINTER_UP) event.actionIndex else -1
            for (i in 0 until event.pointerCount) {
                if (i != lifted) pressed = pressed or layout.buttonsAt(event.getX(i), event.getY(i))
            }
        }
        setMask(pressed)
        return true
    }

    /** The editor: a finger picks the item under it and drags it (the first finger only). */
    private fun edit(layout: TouchLayout, event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val item = layout.itemAt(event.x, event.y)
                selected = item
                onSelect(item)
                dragStart = item?.let(layout::place)
                downX = event.x
                downY = event.y
            }
            MotionEvent.ACTION_MOVE -> {
                val item = selected ?: return true
                val start = dragStart ?: return true
                val moved = layout.with(item, start.copy(x = start.x + (event.x - downX) / width, y = start.y + (event.y - downY) / height))
                onEdit(edits + (item to moved.place(item))) // kept inside the screen
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> dragStart = null
        }
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
        val layout = touchLayout ?: return
        if (hidden && !editing) return
        if (editing) drawPicture(canvas, layout)
        // On the body: the skin's solid colours, darker when pressed, with its rim. Over the game picture: see-through
        // (Faint / Normal / Strong) in the skin's over-picture colours, or each part's own (Plain: white, as before 8b).
        val (idleA, downA, textA) = when (look) {
            Settings.LOOK_FAINT -> Triple(0x33, 0x66, 0x88)
            Settings.LOOK_STRONG -> Triple(0x99, 0xCC, 0xFF)
            else -> Triple(0x55, 0x99, 0xDD)
        }
        val s = skin
        fun alpha(c: Int, a: Int) = (a shl 24) or (c and 0xFFFFFF)
        fun colours(part: Int, ink: Int, over: Boolean) = if (over) {
            val c = s.overPicture ?: part
            Triple(alpha(c, idleA), alpha(c, downA), alpha(s.overPictureText ?: ink, textA))
        } else {
            Triple(part, Skins.pressed(part), ink)
        }
        rim.color = s.rim ?: 0

        // D-pad: the cross, then each pressed arm (outside the middle) again, darker (see-through: lighter over it).
        val dpadOver = if (s.seeThroughBesideGame) layout.dpadOverPicture else layout.dpadOverGame
        val (dIdle, dDown, _) = colours(s.dpad, 0, dpadOver)
        fill.color = dIdle
        canvas.drawPath(dpadShape, fill)
        if (s.rim != null && !dpadOver) canvas.drawPath(dpadShape, rim)
        val x = layout.dpadX
        val y = layout.dpadY
        val r = layout.dpadRadius
        val a = r * ARM
        fill.color = if (dpadOver) dIdle else dDown
        if (mask and KeyMap.UP != 0) canvas.drawRoundRect(x - a, y - r, x + a, y - a, a / 3, a / 3, fill)
        if (mask and KeyMap.DOWN != 0) canvas.drawRoundRect(x - a, y + a, x + a, y + r, a / 3, a / 3, fill)
        if (mask and KeyMap.LEFT != 0) canvas.drawRoundRect(x - r, y - a, x - a, y + a, a / 3, a / 3, fill)
        if (mask and KeyMap.RIGHT != 0) canvas.drawRoundRect(x + a, y - a, x + r, y + a, a / 3, a / 3, fill)

        for (b in layout.buttons) {
            val over = if (s.seeThroughBesideGame) layout.overPicture(b) else layout.overGame(b)
            val (part, partInk) = when (b.bit) {
                KeyMap.A, KeyMap.B -> s.ab to s.abText
                KeyMap.SELECT, KeyMap.START -> s.small to s.smallText
                else -> s.shoulder to s.shoulderText // L, R, rewind, fast-forward
            }
            val (idle, down, ink) = colours(part, partInk, over)
            val lit = b.bit == KeyMap.FAST && fastOn && mask and b.bit == 0
            fill.color = when {
                mask and b.bit != 0 -> down
                lit -> s.lit
                else -> idle
            }
            val edge = s.rim != null && !over
            if (b.round) {
                canvas.drawCircle(b.cx, b.cy, (b.right - b.left) / 2, fill)
                if (edge) canvas.drawCircle(b.cx, b.cy, (b.right - b.left) / 2, rim)
            } else {
                val corner = (b.bottom - b.top) / 2
                canvas.drawRoundRect(b.left, b.top, b.right, b.bottom, corner, corner, fill)
                if (edge) canvas.drawRoundRect(b.left, b.top, b.right, b.bottom, corner, corner, rim)
            }
            text.color = if (lit) s.litText else ink
            text.textSize = b.scale * when {
                b.round -> 22 * density
                b.bit == KeyMap.FAST || b.bit == KeyMap.REWIND -> 20 * density // symbols, not words
                else -> 13 * density
            }
            if (b.bit == KeyMap.FAST) drawFastForward(canvas, b) // ⏩ is a colour emoji: drawn, so it takes the skin's colour
            else canvas.drawText(b.label, b.cx, b.cy - (text.ascent() + text.descent()) / 2, text)
        }
        if (editing) selected?.let { item ->
            val box = layout.box(item)
            val pad = 4 * density
            canvas.drawRect(box.left - pad, box.top - pad, box.right + pad, box.bottom + pad, outline)
        }
    }

    /** The fast-forward symbol: two triangles pointing right, in the label's colour. */
    private fun drawFastForward(canvas: Canvas, b: TouchLayout.Button) {
        val h = 14 * density * b.scale
        val w = h * 0.8f
        fastShape.reset()
        for (i in 0..1) {
            val x = b.cx - w + i * w
            fastShape.moveTo(x, b.cy - h / 2)
            fastShape.lineTo(x + w, b.cy)
            fastShape.lineTo(x, b.cy + h / 2)
            fastShape.close()
        }
        fill.color = text.color
        canvas.drawPath(fastShape, fill)
    }

    /** The editor: the paused game's picture fitted in the picture's box, or a plain block with its name. */
    private fun drawPicture(canvas: Canvas, layout: TouchLayout) {
        val box = layout.picture
        val frame = picture
        if (frame != null) {
            val fit = run { // in the picture's own shape inside the box
                val w = box.right - box.left
                val h = box.bottom - box.top
                val fw = if (w * frame.height <= h * frame.width) w else h * frame.width / frame.height
                val fh = fw * frame.height / frame.width
                RectF(box.left + (w - fw) / 2, box.top + (h - fh) / 2, box.left + (w + fw) / 2, box.top + (h + fh) / 2)
            }
            canvas.drawBitmap(frame, Rect(0, 0, frame.width, frame.height), fit, null)
        } else {
            fill.color = PICTURE_BLOCK
            canvas.drawRect(box.left, box.top, box.right, box.bottom, fill)
            text.color = 0xFF9C998F.toInt()
            text.textSize = 16 * density
            canvas.drawText("Game picture", box.cx, box.cy, text)
        }
    }

    private companion object {
        const val ARM = 0.36f // the D-pad's arm half-width, as a part of its radius
        const val PICTURE_BLOCK = 0xFF2A2C31.toInt()
    }
}
