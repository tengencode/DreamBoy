package com.tengen.gbaemu

import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.roundToInt

/** What the layout editor can move: the game picture, the D-pad (as one) and each button. */
enum class LayoutItem(val id: String, val label: String) {
    PICTURE("picture", "Game picture"), DPAD("dpad", "D-pad"), L("l", "L"), R("r", "R"), REWIND("rewind", "Rewind"),
    FAST("fast", "Fast-forward"), A("a", "A"), B("b", "B"), SELECT("select", "Select"), START("start", "Start");

    companion object {
        fun byId(id: String): LayoutItem? = entries.firstOrNull { it.id == id }
    }
}

/** An edited item: its centre as shares of the screen's width and height (0..1) and its size (1 = the default). */
data class Place(val x: Float, val y: Float, val scale: Float = 1f)

/**
 * Where the game picture and the touch buttons are, in pixels, on a [width] × [height] screen. With no [edits] it is
 * the layout of 4c: upright (layout A) the picture full width at the top (3:2) and the buttons in the area below it;
 * sideways the picture's box is the whole screen (the game fitted inside) and the buttons over the side bars —
 * ⟲ rewind and ⏩ fast-forward (KeyMap.REWIND / FAST, not GBA buttons) upright between L and R, sideways just below L
 * and R. [edits] (the layout editor, 6c) move items anywhere and size them ½×–2×, always inside the screen.
 * [fitMode] (Settings.SIZE_*) places the game itself inside the picture's box ([game], 8b).
 * [console] gives the picture's shape (3:2, or 10:9 for a Game Boy) and says whether there are L and R buttons.
 * Pure maths (no Android classes), so it is tested on the PC.
 */
class TouchLayout(val width: Float, val height: Float, private val density: Float, val sideways: Boolean,
                  val edits: Map<LayoutItem, Place> = emptyMap(), val fitMode: Int = Settings.SIZE_FIT,
                  val console: Console = Console.GBA) {
    data class Box(val left: Float, val top: Float, val right: Float, val bottom: Float) {
        val cx get() = (left + right) / 2
        val cy get() = (top + bottom) / 2

        fun overlaps(o: Box) = left < o.right && o.left < right && top < o.bottom && o.top < bottom
    }

    /** One button: [round] ones are the circle inside the box, the others a rounded box; [scale] its size. */
    data class Button(val bit: Int, val label: String, val round: Boolean,
                      val left: Float, val top: Float, val right: Float, val bottom: Float, val scale: Float = 1f) {
        val cx get() = (left + right) / 2
        val cy get() = (top + bottom) / 2
        val box get() = Box(left, top, right, bottom)
    }

    private val dp = density
    private val slop = 8 * dp // fingers are imprecise: a button reacts a little outside its drawing

    val picture: Box
    val dpadX: Float
    val dpadY: Float
    val dpadRadius: Float
    val buttons: List<Button>
    private val scales = HashMap<LayoutItem, Float>()
    /** The buttons this console has, in drawing order (a Game Boy has no L and R). */
    private val shown = BUTTONS.filter { console.hasShoulders || (it.first != LayoutItem.L && it.first != LayoutItem.R) }

    /** What the layout editor can move here. */
    val items: List<LayoutItem> get() = listOf(LayoutItem.PICTURE, LayoutItem.DPAD) + shown.map { it.first }

    init {
        val defaults = defaultBoxes()
        picture = placed(LayoutItem.PICTURE, defaults.getValue(LayoutItem.PICTURE))
        val dpad = placed(LayoutItem.DPAD, defaults.getValue(LayoutItem.DPAD))
        dpadX = dpad.cx
        dpadY = dpad.cy
        dpadRadius = (dpad.right - dpad.left) / 2
        buttons = shown.map { (item, bit, label) ->
            val b = placed(item, defaults.getValue(item))
            Button(bit, label, bit == KeyMap.A || bit == KeyMap.B, b.left, b.top, b.right, b.bottom, scales.getValue(item))
        }
    }

    /** 4c's boxes: upright the area below the picture, sideways the whole screen. */
    private fun defaultBoxes(): Map<LayoutItem, Box> {
        val m = 16 * dp
        val boxes = HashMap<LayoutItem, Box>()
        if (!sideways) {
            val top = width * console.frameHeight / console.frameWidth // the picture: full width, in its shape, at the top
            boxes[LayoutItem.PICTURE] = Box(0f, 0f, width, top)
            val h = height - top // the controls area below it
            val shoulderH = 40 * dp
            val smallW = 80 * dp
            val smallH = 32 * dp
            val smallTop = h - m - smallH
            val r = 70 * dp
            val dx = m + r
            val dy = (m + shoulderH + smallTop) / 2
            boxes[LayoutItem.DPAD] = Box(dx - r, top + dy - r, dx + r, top + dy + r)
            val ar = 36 * dp
            val ax = width - m - ar
            val ay = dy - 28 * dp
            val bx = ax - 84 * dp
            val by = ay + 56 * dp
            val extraW = 64 * dp
            val extraTop = m + 4 * dp
            val extraBottom = m + shoulderH - 4 * dp
            boxes[LayoutItem.L] = Box(0f, top + m, 96 * dp, top + m + shoulderH)
            boxes[LayoutItem.R] = Box(width - 96 * dp, top + m, width, top + m + shoulderH)
            boxes[LayoutItem.REWIND] = Box(width / 2 - 8 * dp - extraW, top + extraTop, width / 2 - 8 * dp, top + extraBottom)
            boxes[LayoutItem.FAST] = Box(width / 2 + 8 * dp, top + extraTop, width / 2 + 8 * dp + extraW, top + extraBottom)
            boxes[LayoutItem.A] = Box(ax - ar, top + ay - ar, ax + ar, top + ay + ar)
            boxes[LayoutItem.B] = Box(bx - ar, top + by - ar, bx + ar, top + by + ar)
            boxes[LayoutItem.SELECT] = Box(width / 2 - 8 * dp - smallW, top + smallTop, width / 2 - 8 * dp, top + smallTop + smallH)
            boxes[LayoutItem.START] = Box(width / 2 + 8 * dp, top + smallTop, width / 2 + 8 * dp + smallW, top + smallTop + smallH)
        } else {
            boxes[LayoutItem.PICTURE] = Box(0f, 0f, width, height) // the game is fitted inside
            val shoulderH = 40 * dp
            val smallW = 72 * dp
            val smallH = 28 * dp
            val smallTop = height - m - smallH
            val r = 60 * dp
            val dx = m + r
            val dy = height * 0.55f
            boxes[LayoutItem.DPAD] = Box(dx - r, dy - r, dx + r, dy + r)
            val ar = 30 * dp
            val ax = width - m - ar
            val ay = height * 0.45f
            val bx = ax - 66 * dp
            val by = ay + 52 * dp
            val extraTop = m + shoulderH + 12 * dp
            val extraBottom = extraTop + 32 * dp
            boxes[LayoutItem.L] = Box(0f, m, 96 * dp, m + shoulderH)
            boxes[LayoutItem.R] = Box(width - 96 * dp, m, width, m + shoulderH)
            boxes[LayoutItem.REWIND] = Box(16 * dp, extraTop, 80 * dp, extraBottom)
            boxes[LayoutItem.FAST] = Box(width - 80 * dp, extraTop, width - 16 * dp, extraBottom)
            boxes[LayoutItem.A] = Box(ax - ar, ay - ar, ax + ar, ay + ar)
            boxes[LayoutItem.B] = Box(bx - ar, by - ar, bx + ar, by + ar)
            if (console.hasShoulders) {
                boxes[LayoutItem.SELECT] = Box(width / 2 - 8 * dp - smallW, smallTop, width / 2 - 8 * dp, smallTop + smallH)
                boxes[LayoutItem.START] = Box(width / 2 + 8 * dp, smallTop, width / 2 + 8 * dp + smallW, smallTop + smallH)
            } else { // a Game Boy picture is narrow: the side bars have room, so nothing covers the game
                boxes[LayoutItem.SELECT] = Box(16 * dp, smallTop, 16 * dp + smallW, smallTop + smallH)
                boxes[LayoutItem.START] = Box(width - 16 * dp - smallW, smallTop, width - 16 * dp, smallTop + smallH)
            }
        }
        return boxes
    }

    /** [default] moved and sized by the item's edit, kept inside the screen (never bigger than it). */
    private fun placed(item: LayoutItem, default: Box): Box {
        val e = edits[item]
        val w0 = default.right - default.left
        val h0 = default.bottom - default.top
        val s = min(clampScale(e?.scale ?: 1f), min(width / w0, height / h0))
        scales[item] = s
        val w = w0 * s
        val h = h0 * s
        val cx = (e?.x?.times(width) ?: default.cx).coerceIn(w / 2, width - w / 2)
        val cy = (e?.y?.times(height) ?: default.cy).coerceIn(h / 2, height - h / 2)
        return Box(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
    }

    /** The same layout with [item] at [place] (its size brought into ½–2). */
    fun with(item: LayoutItem, place: Place): TouchLayout =
        TouchLayout(width, height, density, sideways, edits + (item to place.copy(scale = clampScale(place.scale))), fitMode, console)

    /** Where [item] is now, as a Place (for the editor). */
    fun place(item: LayoutItem): Place {
        val b = box(item)
        return Place(b.cx / width, b.cy / height, scales.getValue(item))
    }

    fun box(item: LayoutItem): Box = when (item) {
        LayoutItem.PICTURE -> picture
        LayoutItem.DPAD -> Box(dpadX - dpadRadius, dpadY - dpadRadius, dpadX + dpadRadius, dpadY + dpadRadius)
        else -> buttons[shown.indexOfFirst { it.first == item }].box
    }

    /** Over the game picture's box (sideways: the whole screen). Plain draws these see-through, as before 8b. */
    fun overPicture(b: Button) = b.box.overlaps(picture)
    val dpadOverPicture get() = box(LayoutItem.DPAD).overlaps(picture)

    /** Where the game itself is drawn inside the picture's box (the rest is the skin's body). */
    val game: Box get() = gameIn(picture, fitMode, console)

    /** Over the game itself: the skins (8b) draw these see-through, the others solid on the body. */
    fun overGame(b: Button) = b.box.overlaps(game)
    val dpadOverGame get() = box(LayoutItem.DPAD).overlaps(game)

    /** The editor: the item under a finger at (x, y) — a button, else the D-pad, else the picture — or null. */
    fun itemAt(x: Float, y: Float): LayoutItem? {
        buttons.forEachIndexed { i, b -> if (inside(b, x, y, 0f)) return shown[i].first }
        if (hypot(x - dpadX, y - dpadY) <= dpadRadius) return LayoutItem.DPAD
        if (x >= picture.left && x <= picture.right && y >= picture.top && y <= picture.bottom) return LayoutItem.PICTURE
        return null
    }

    /** The GBA buttons a finger at (x, y) presses: D-pad directions, or the nearest button it touches, or 0. */
    fun buttonsAt(x: Float, y: Float): Int {
        val dx = x - dpadX
        val dy = y - dpadY
        val dist = hypot(dx, dy)
        if (dist <= dpadRadius + slop) return if (dist < dpadRadius * DEAD_ZONE) 0 else dpadDirection(dx, dy)
        var best = 0
        var bestDist = Float.MAX_VALUE
        for (b in buttons) {
            if (!inside(b, x, y, slop)) continue
            val d = hypot(x - b.cx, y - b.cy)
            if (d < bestDist) {
                best = b.bit
                bestDist = d
            }
        }
        return best
    }

    private fun inside(b: Button, x: Float, y: Float, margin: Float): Boolean =
        if (b.round) hypot(x - b.cx, y - b.cy) <= (b.right - b.left) / 2 + margin
        else x >= b.left - margin && x <= b.right + margin && y >= b.top - margin && y <= b.bottom + margin

    companion object {
        const val DEAD_ZONE = 0.25f // no direction in the middle quarter of the D-pad's radius
        const val MIN_SCALE = 0.5f
        const val MAX_SCALE = 2f

        /** The buttons in drawing order: the item, its bit, its label. */
        private val BUTTONS = listOf(
            Triple(LayoutItem.L, KeyMap.L, "L"), Triple(LayoutItem.R, KeyMap.R, "R"),
            Triple(LayoutItem.REWIND, KeyMap.REWIND, "⟲"), Triple(LayoutItem.FAST, KeyMap.FAST, "⏩"),
            Triple(LayoutItem.A, KeyMap.A, "A"), Triple(LayoutItem.B, KeyMap.B, "B"),
            Triple(LayoutItem.SELECT, KeyMap.SELECT, "SELECT"), Triple(LayoutItem.START, KeyMap.START, "START"),
        )

        fun clampScale(s: Float) = s.coerceIn(MIN_SCALE, MAX_SCALE)

        /** The game inside [box], as OpenGL draws it on a surface of the box's size (fit_rect_mode, platform/android/fit_rect.c). */
        fun gameIn(box: Box, fitMode: Int, console: Console = Console.GBA): Box {
            val fw = console.frameWidth
            val fh = console.frameHeight
            val w = (box.right - box.left).roundToInt()
            val h = (box.bottom - box.top).roundToInt()
            if (w <= 0 || h <= 0 || fitMode == Settings.SIZE_STRETCH) return box
            fun centred(gw: Int, gh: Int): Box {
                val x = box.left + (w - gw) / 2
                val y = box.top + (h - gh) / 2
                return Box(x, y, x + gw, y + gh)
            }
            if (fitMode == Settings.SIZE_INTEGER) {
                val n = min(w / fw, h / fh)
                if (n >= 1) return centred(fw * n, fh * n)
            }
            return if (w * fh <= h * fw) centred(w, w * fh / fw) else centred(h * fw / fh, h)
        }

        /** 8 sectors of 45°, from the D-pad's centre (screen y grows downwards): diagonals press two directions. */
        fun dpadDirection(dx: Float, dy: Float): Int {
            val angle = Math.toDegrees(atan2(-dy, dx).toDouble()) // 0 = right, 90 = up
            return when (((angle / 45.0).roundToInt() + 8) % 8) {
                0 -> KeyMap.RIGHT
                1 -> KeyMap.RIGHT or KeyMap.UP
                2 -> KeyMap.UP
                3 -> KeyMap.UP or KeyMap.LEFT
                4 -> KeyMap.LEFT
                5 -> KeyMap.LEFT or KeyMap.DOWN
                6 -> KeyMap.DOWN
                else -> KeyMap.DOWN or KeyMap.RIGHT
            }
        }
    }
}
