package com.tengen.gbaemu

import kotlin.math.hypot
import kotlin.math.min

/**
 * A Delta skin's layout [face] on a [width] × [height] screen, in pixels (9): the picture scaled to fit whole —
 * upright at the bottom, centred across; sideways centred — the game's box (the skin's screen; without one, upright the
 * area above the picture, sideways the whole screen), what a finger presses, and the strips the picture leaves at the
 * screen's edges. Pure maths, so it is tested on the PC.
 */
class DeltaLayout(val face: DeltaSkin.Face, val width: Float, val height: Float, val sideways: Boolean) {
    /** What a finger is on: the item and, for a D-pad, the directions of its cell (else the item's buttons). */
    data class Hit(val item: DeltaSkin.Item, val bits: Int)

    val scale = min(width / face.width, height / face.height)
    val picture: TouchLayout.Box
    val game: TouchLayout.Box

    init {
        val w = face.width * scale
        val h = face.height * scale
        val left = (width - w) / 2
        val top = if (sideways) (height - h) / 2 else height - h
        picture = TouchLayout.Box(left, top, left + w, top + h)
        game = face.screen?.let(::box)
            ?: if (sideways) TouchLayout.Box(0f, 0f, width, height) else TouchLayout.Box(0f, 0f, width, top)
    }

    /** A skin rectangle on the screen. */
    fun box(r: DeltaSkin.Rect) = TouchLayout.Box(picture.left + r.x * scale, picture.top + r.y * scale,
        picture.left + (r.x + r.width) * scale, picture.top + (r.y + r.height) * scale)

    /** The item a finger at (x, y) is on: of those whose area (frame + extended edges) holds it, the nearest centre. */
    fun at(x: Float, y: Float): Hit? {
        var best: DeltaSkin.Item? = null
        var bestDistance = Float.MAX_VALUE
        for (item in face.items) {
            val b = box(item.frame)
            val e = item.extended
            if (x < b.left - e.left * scale || x > b.right + e.right * scale ||
                y < b.top - e.top * scale || y > b.bottom + e.bottom * scale) continue
            val d = hypot(x - b.cx, y - b.cy)
            if (d < bestDistance) {
                best = item
                bestDistance = d
            }
        }
        val item = best ?: return null
        if (!item.dpad) return Hit(item, item.bits)
        val b = box(item.frame) // 3 × 3: the middle nothing, the sides one direction, the corners two
        val column = ((x - b.left) / (b.right - b.left) * 3).toInt().coerceIn(0, 2)
        val row = ((y - b.top) / (b.bottom - b.top) * 3).toInt().coerceIn(0, 2)
        var bits = 0
        if (row == 0) bits = bits or KeyMap.UP
        if (row == 2) bits = bits or KeyMap.DOWN
        if (column == 0) bits = bits or KeyMap.LEFT
        if (column == 2) bits = bits or KeyMap.RIGHT
        return Hit(item, bits and item.bits)
    }

    /** The strips between the picture and the screen's edges ([TOP], [BOTTOM], [LEFT], [RIGHT]), except over the game. */
    fun leftovers(): List<Pair<Int, TouchLayout.Box>> = listOf(
        TOP to TouchLayout.Box(0f, 0f, width, picture.top),
        BOTTOM to TouchLayout.Box(0f, picture.bottom, width, height),
        LEFT to TouchLayout.Box(0f, picture.top, picture.left, picture.bottom),
        RIGHT to TouchLayout.Box(picture.right, picture.top, width, picture.bottom),
    ).filter { (_, b) -> b.right - b.left >= 0.5f && b.bottom - b.top >= 0.5f && !b.overlaps(game) }

    companion object {
        const val TOP = 0
        const val BOTTOM = 1
        const val LEFT = 2
        const val RIGHT = 3

        /** A nearly square item is a round button: darkened as the oval inside its frame. */
        fun round(r: DeltaSkin.Rect) = r.width / r.height in 0.8f..1.25f
    }
}

/**
 * The fingers on a Delta skin (9): the buttons held ([mask]), the items under a finger ([held], darkened), and the
 * actions to fire — once, when a new finger lands on the item (not when one slides onto it, nor while it is held).
 */
class DeltaTouch(private val layout: DeltaLayout) {
    private var on = HashMap<Int, DeltaSkin.Item>()
    private var fingers = emptySet<Int>()

    var mask = 0
        private set

    val held: Collection<DeltaSkin.Item> get() = on.values

    /** [now]: pointer id → (x, y) of every finger down. Returns the actions to fire. */
    fun update(now: Map<Int, Pair<Float, Float>>): List<DeltaSkin.Action> {
        val items = HashMap<Int, DeltaSkin.Item>()
        var bits = 0
        for ((id, point) in now) {
            val hit = layout.at(point.first, point.second) ?: continue
            bits = bits or hit.bits
            items[id] = hit.item
        }
        val fire = items.filterKeys { it !in fingers }.values.flatMap { it.actions }
        on = items
        fingers = now.keys.toSet()
        mask = bits
        return fire
    }
}
