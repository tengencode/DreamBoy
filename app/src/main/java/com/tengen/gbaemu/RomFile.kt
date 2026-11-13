package com.tengen.gbaemu

import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.util.zip.ZipInputStream

/**
 * A game's bytes from a game file (`.gba`, `.gb`, `.gbc`), or from the first game file inside a `.zip` (told apart by
 * the zip's first bytes).
 */
object RomFile {
    const val MAX_SIZE = 32 * 1024 * 1024
    val GAME_ENDINGS = listOf(".gba", ".gb", ".gbc")

    private fun isGameName(name: String) = GAME_ENDINGS.any { name.endsWith(it, ignoreCase = true) }

    /** Not a game this app can run: empty, over 32 MiB, or a zip without a game file inside. */
    class NotAGame(message: String) : Exception(message)

    /** A game's bytes and, when it came out of a zip, its file name in there (else null). */
    class Named(val bytes: ByteArray, val zipName: String?)

    /** Reads the game (closes nothing: the caller owns [input]). Throws [NotAGame], or an IOException if reading fails. */
    fun read(input: InputStream): ByteArray = readNamed(input).bytes

    fun readNamed(input: InputStream): Named {
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
                val e = zip.nextEntry ?: throw NotAGame("no game file inside this zip")
                if (!e.isDirectory && isGameName(e.name)) return Named(limited(zip), e.name)
            }
        }
        return Named(limited(buf), null)
    }

    /** The name of the first game file in the zip [input] (reads only as far as that name), or null: none, or no zip. */
    fun zipGameName(input: InputStream): String? = try {
        val zip = ZipInputStream(BufferedInputStream(input))
        generateSequence { zip.nextEntry }.firstOrNull { !it.isDirectory && isGameName(it.name) }?.name
    } catch (e: Exception) {
        null
    }

    fun zipHasGame(input: InputStream): Boolean = zipGameName(input) != null

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
