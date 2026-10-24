package com.tengen.gbaemu

import java.io.File
import java.io.InputStream
import java.util.zip.CRC32

/** A BIOS file the user chose, copied to `files/bios/gba_bios.bin` (Settings → BIOS). */
object Bios {
    const val SIZE = 16384
    private const val USUAL_CRC = 0x81977335L // the GBA BIOS every console has

    enum class Check { USUAL, UNUSUAL, WRONG_SIZE }

    fun check(bytes: ByteArray): Check = when {
        bytes.size != SIZE -> Check.WRONG_SIZE
        CRC32().apply { update(bytes) }.value == USUAL_CRC -> Check.USUAL
        else -> Check.UNUSUAL
    }

    fun file(filesDir: File) = File(filesDir, "bios/gba_bios.bin")

    /** A picked file's bytes, or null when it is bigger than a BIOS (not read whole). */
    fun read(input: InputStream): ByteArray? = Net.readUpTo(input, SIZE)

    /** Copies [bytes] in (safely: a temporary file, then swapped); false (nothing written) if not 16 KiB. */
    fun install(filesDir: File, bytes: ByteArray): Boolean = bytes.size == SIZE && file(filesDir).writeSafely(bytes)

    /** The copied BIOS, or null (none, or not 16 KiB). */
    fun load(filesDir: File): ByteArray? = file(filesDir).takeIf { it.isFile }?.readBytes()?.takeIf { it.size == SIZE }

    fun remove(filesDir: File) {
        file(filesDir).delete()
    }
}
