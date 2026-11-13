package com.tengen.gbaemu

import kotlin.math.max
import kotlin.math.min

/** Why a skin can't be imported or used (9): the message the user sees. */
enum class SkinProblem(val message: String) {
    NOT_A_SKIN("This file isn't a skin."),
    OTHER_CONSOLE("This skin is for another console. Only Game Boy, Game Boy Color and GBA skins work."),
    NO_PHONE_LAYOUT("This skin has no phone layout."),
    PICTURE_MISSING("This skin's picture is missing."),
    TOO_BIG("This skin is too big."),
}

class SkinException(val problem: SkinProblem) : Exception(problem.message)

/**
 * A skin made for the Delta emulator (9), read from its `info.json`: the phone layout for each direction — upright
 * (Delta's portrait) and sideways (landscape), each from `iphone.edgeToEdge` (the tall iPhones, closest to Android
 * phones), else `iphone.standard`; either may be missing. Coordinates are Delta's points in the picture's
 * `mappingSize`. [family] is the console family it was made for ([Console.family]): Delta has one skin type for Game Boy
 * and Game Boy Color, whose L and R press nothing. Pure Kotlin, so it is tested on the PC.
 */
class DeltaSkin(val name: String, val identifier: String, val family: String, val upright: Face?, val sideways: Face?) {
    enum class Action { MENU, QUICK_SAVE, QUICK_LOAD }

    data class Rect(val x: Float, val y: Float, val width: Float, val height: Float)
    data class Edges(val top: Float = 0f, val bottom: Float = 0f, val left: Float = 0f, val right: Float = 0f)

    /** A touch area: the buttons it presses (KeyMap bits, FAST too), the actions it fires; a [dpad] is split 3 × 3. */
    data class Item(val bits: Int, val actions: List<Action>, val dpad: Boolean, val frame: Rect, val extended: Edges)

    /**
     * One layout: its picture's file name (PDF or PNG), the picture's size, the touch areas, where the game goes (null:
     * the skin doesn't say) and whether the picture is see-through.
     */
    data class Face(val image: String, val width: Float, val height: Float, val items: List<Item>, val screen: Rect?,
                    val translucent: Boolean)

    fun face(sideways: Boolean): Face? = if (sideways) this.sideways else upright

    companion object {
        const val GBA = "com.rileytestut.delta.game.gba"
        const val GBC = "com.rileytestut.delta.game.gbc"

        /** Reads `info.json`; throws [SkinException] (NOT_A_SKIN, OTHER_CONSOLE, NO_PHONE_LAYOUT). */
        fun parse(json: String): DeltaSkin {
            val root = try {
                Json.Reader(json.removePrefix("﻿")).run { value().also { end() } }
            } catch (e: IllegalStateException) {
                null
            } catch (e: StackOverflowError) { // nested too deep
                null
            }
            val m = map(root)
            val name = m["name"] as? String ?: fail(SkinProblem.NOT_A_SKIN)
            val id = m["identifier"] as? String ?: fail(SkinProblem.NOT_A_SKIN)
            val type = m["gameTypeIdentifier"] as? String ?: fail(SkinProblem.NOT_A_SKIN)
            val console = when {
                type.equals(GBA, ignoreCase = true) -> Console.GBA
                type.equals(GBC, ignoreCase = true) -> Console.GB
                else -> fail(SkinProblem.OTHER_CONSOLE)
            }
            val keys = if (console.hasShoulders) -1 else (KeyMap.L or KeyMap.R).inv()
            val iphone = (m["representations"] as? Map<*, *>)?.get("iphone") as? Map<*, *>
            fun layout(direction: String): Face? = listOf("edgeToEdge", "standard").firstNotNullOfOrNull {
                ((iphone?.get(it) as? Map<*, *>)?.get(direction) as? Map<*, *>)
            }?.let { face(it, keys) }
            val upright = layout("portrait")
            val sideways = layout("landscape")
            if (upright == null && sideways == null) fail(SkinProblem.NO_PHONE_LAYOUT)
            return DeltaSkin(name, id, console.family, upright, sideways)
        }

        private val INPUTS = mapOf(
            "a" to KeyMap.A, "b" to KeyMap.B, "l" to KeyMap.L, "r" to KeyMap.R, "select" to KeyMap.SELECT, "start" to KeyMap.START,
            "up" to KeyMap.UP, "down" to KeyMap.DOWN, "left" to KeyMap.LEFT, "right" to KeyMap.RIGHT,
            "fastForward" to KeyMap.FAST, "toggleFastForward" to KeyMap.FAST, // our ⏩ is tap or hold
        )
        private val ACTIONS = mapOf("menu" to Action.MENU, "quickSave" to Action.QUICK_SAVE, "quickLoad" to Action.QUICK_LOAD)

        private fun fail(p: SkinProblem): Nothing = throw SkinException(p)

        private fun map(v: Any?): Map<*, *> = v as? Map<*, *> ?: fail(SkinProblem.NOT_A_SKIN)

        private fun num(m: Map<*, *>, key: String): Float = (m[key] as? Double)?.toFloat() ?: fail(SkinProblem.NOT_A_SKIN)

        private fun rect(v: Any?): Rect = map(v).let { Rect(num(it, "x"), num(it, "y"), num(it, "width"), num(it, "height")) }

        /** [v]'s sides; a side it doesn't give is [d]'s. */
        private fun edges(v: Any?, d: Edges): Edges {
            val m = v as? Map<*, *> ?: return d
            fun side(key: String, default: Float) = (m[key] as? Double)?.toFloat() ?: default
            return Edges(side("top", d.top), side("bottom", d.bottom), side("left", d.left), side("right", d.right))
        }

        private fun face(m: Map<*, *>, keys: Int): Face {
            val assets = map(m["assets"])
            val image = listOf("resizable", "large", "medium", "small").firstNotNullOfOrNull { assets[it] as? String }
                ?: fail(SkinProblem.NOT_A_SKIN)
            val size = map(m["mappingSize"])
            val extended = edges(m["extendedEdges"], Edges())
            val items = (m["items"] as? List<*> ?: emptyList<Any?>()).mapNotNull { item(map(it), extended, keys) }
            val screen = ((m["screens"] as? List<*>)?.firstOrNull() as? Map<*, *>)?.get("outputFrame")?.let(::rect)
                ?: m["gameScreenFrame"]?.let(::rect)
            val w = num(size, "width")
            val h = num(size, "height")
            if (!(w > 0 && h > 0 && w.isFinite() && h.isFinite() && max(w, h) <= 20 * min(w, h))) fail(SkinProblem.NOT_A_SKIN)
            return Face(image, w, h, items, screen, m["translucent"] == true)
        }

        /**
         * An item, or null when none of its inputs is one we use (X, Y, the analog stick, the touch screen, ...);
         * [keys]: the buttons the skin's console has.
         */
        private fun item(m: Map<*, *>, extended: Edges, keys: Int): Item? {
            val inputs = m["inputs"]
            val names = when (inputs) {
                is Map<*, *> -> inputs.values.mapNotNull { it as? String } // a D-pad: {"up": "up", ...}
                is List<*> -> inputs.mapNotNull { it as? String }
                else -> emptyList()
            }
            var bits = 0
            val actions = ArrayList<Action>()
            for (n in names) {
                INPUTS[n]?.let { bits = bits or it }
                ACTIONS[n]?.let { actions += it }
            }
            bits = bits and keys
            if (bits == 0 && actions.isEmpty()) return null
            return Item(bits, actions, inputs is Map<*, *>, rect(m["frame"]), edges(m["extendedEdges"], extended))
        }
    }
}
