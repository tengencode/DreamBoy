package com.tengen.gbaemu

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.File

/**
 * One game's save states in `<files>/states/<game id>/`: slots 1-10, the automatic one ([AUTO]) and the Quick one
 * ([QUICK], a Delta skin's quick save, 9), each a `.state` file with a `.png` picture of that moment beside it. Saving
 * and loading need the game paused.
 */
class States(filesDir: File, gameId: String) {
    val dir = File(filesDir, "states/$gameId")

    fun state(slot: Int): File = File(dir, name(slot) + ".state")
    fun picture(slot: Int): File = File(dir, name(slot) + ".png")
    fun used(slot: Int): Boolean = state(slot).exists()
    fun savedAt(slot: Int): Long = state(slot).lastModified()

    /** The paused game into [slot], with its picture. False: nothing saved (an older state there stays). */
    fun save(slot: Int): Boolean {
        dir.mkdirs()
        val frame = NativeBridge.frameBgr555() ?: return false
        if (!NativeBridge.saveState(state(slot).path)) return false
        try {
            picture(slot).outputStream().use { bitmapOf(frame).compress(Bitmap.CompressFormat.PNG, 100, it) }
        } catch (e: Exception) { // the state is what matters; a missing picture shows as a plain tile
        }
        return true
    }

    /** False: refused (another game, another version, damaged) — the game is unchanged. */
    fun load(slot: Int): Boolean = NativeBridge.loadState(state(slot).path) == 0

    fun loadPicture(slot: Int): Bitmap? = picture(slot).takeIf { it.exists() }?.let { BitmapFactory.decodeFile(it.path) }

    companion object {
        const val AUTO = 0
        const val SLOTS = 10
        const val QUICK = 11

        fun name(slot: Int): String = when (slot) {
            AUTO -> "auto"
            QUICK -> "quick"
            else -> "slot$slot"
        }

        /** A BGR555 picture as ARGB pixels (5 bits → 8 by repeating the top bits, like the OpenGL shader). */
        fun argbOf(bgr555: ShortArray): IntArray = IntArray(bgr555.size) { i ->
            val c = bgr555[i].toInt()
            fun eight(v: Int) = (v shl 3) or (v shr 2)
            (0xFF shl 24) or (eight(c and 31) shl 16) or (eight((c shr 5) and 31) shl 8) or eight((c shr 10) and 31)
        }

        fun bitmapOf(bgr555: ShortArray): Bitmap =
            Bitmap.createBitmap(argbOf(bgr555), 240, 160, Bitmap.Config.ARGB_8888)
    }
}
