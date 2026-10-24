package com.tengen.gbaemu

import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.util.zip.ZipInputStream

/** A game's bytes from a `.gba` file, or from the first `.gba` inside a `.zip` (told apart by the zip's first bytes). */
object RomFile {
    const val MAX_SIZE = 32 * 1024 * 1024

    /** Not a game this app can run: empty, over 32 MiB, or a zip without a `.gba` inside. */
    class NotAGame(message: String) : Exception(message)

    /** Reads the game (closes nothing: the caller owns [input]). Throws [NotAGame], or an IOException if reading fails. */
    fun read(input: InputStream): ByteArray {
        val buf = BufferedInputStream(input)
        buf.mark(4)
        val head = ByteArray(4)
        var n = 0
        while (n < 4) {
            val r = buf.read(head, n, 4 - n)
            if (r < 0) break
            n += r
        }
        buf.reset()
        if (n == 4 && isZip(head)) {
            val zip = ZipInputStream(buf)
            while (true) {
                val e = zip.nextEntry ?: throw NotAGame("no .gba file inside this zip")
                if (!e.isDirectory && e.name.endsWith(".gba", ignoreCase = true)) return limited(zip)
            }
        }
        return limited(buf)
    }

    /** True if [input] is a zip holding a `.gba` file (reads only as far as that file's name). */
    fun zipHasGame(input: InputStream): Boolean = try {
        val zip = ZipInputStream(BufferedInputStream(input))
        generateSequence { zip.nextEntry }.any { !it.isDirectory && it.name.endsWith(".gba", ignoreCase = true) }
    } catch (e: Exception) {
        false
    }

    private fun isZip(head: ByteArray) = head[0] == 'P'.code.toByte() && head[1] == 'K'.code.toByte() &&
        head[2] == 3.toByte() && head[3] == 4.toByte()

    /** All of [input], refusing more than [MAX_SIZE] bytes (and nothing at all). */
    private fun limited(input: InputStream): ByteArray {
        val out = ByteArrayOutputStream()
        val chunk = ByteArray(64 * 1024)
        while (true) {
            val n = input.read(chunk)
            if (n < 0) break
            out.write(chunk, 0, n)
            if (out.size() > MAX_SIZE) throw NotAGame("over 32 MiB")
        }
        if (out.size() == 0) throw NotAGame("an empty file")
        return out.toByteArray()
    }
}
