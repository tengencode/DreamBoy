package com.tengen.gbaemu

import java.io.File
import java.io.InputStream

/**
 * A game's save file `files/saves/<id>.sav`, in and out of the app from the library's hold menu (no game running).
 * The save itself is written by the emulation thread while a game runs.
 */
object SaveFiles {
    private val SIZES = setOf(512, 8192, 32768, 65536, 131072) // EEPROM 512 B / 8 KiB, SRAM, Flash 64 / 128 KiB

    enum class Import { OK, WRONG_SIZE, FAILED }

    fun validSize(size: Int): Boolean = size in SIZES || size - 16 in SIZES // + mGBA's 16-byte clock footer

    /** A picked file's bytes, or null when it is bigger than any save (not read whole: a video picked by mistake). */
    fun read(input: InputStream): ByteArray? = Net.readUpTo(input, SIZES.max() + 16)

    fun file(filesDir: File, id: String) = File(filesDir, "saves/$id.sav")

    /** The save's bytes, or null if the game has none yet. */
    fun export(filesDir: File, id: String): ByteArray? = file(filesDir, id).takeIf { it.isFile }?.readBytes()

    /**
     * [bytes] become the game's save: the old one kept as `.sav.bak`, the new one written safely. The automatic
     * state is set aside (`auto.state.old`, `auto.png.old`): a state carries the save RAM, so resuming it would
     * bring the old save back. The next start is from the title with this save.
     */
    fun import(filesDir: File, id: String, bytes: ByteArray): Import {
        if (!validSize(bytes.size)) return Import.WRONG_SIZE
        return try {
            val sav = file(filesDir, id)
            if (sav.isFile) sav.copyTo(File(sav.path + ".bak"), overwrite = true)
            if (!sav.writeSafely(bytes)) return Import.FAILED
            val states = States(filesDir, id)
            for (f in listOf(states.state(States.AUTO), states.picture(States.AUTO))) {
                if (f.exists()) {
                    val old = File(f.path + ".old")
                    old.delete()
                    f.renameTo(old)
                }
            }
            Import.OK
        } catch (e: Exception) {
            Import.FAILED
        }
    }
}
