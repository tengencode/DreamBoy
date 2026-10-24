package com.tengen.gbaemu

import java.util.zip.CRC32

/** A ROM's fingerprint, the name of its save file: the CRC32 of its bytes as 8 lowercase hex digits. */
object GameId {
    fun of(rom: ByteArray): String = "%08x".format(CRC32().apply { update(rom) }.value)
}
