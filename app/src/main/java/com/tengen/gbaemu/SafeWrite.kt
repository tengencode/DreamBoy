package com.tengen.gbaemu

import java.io.File

/** Writes [bytes] here safely: into `<name>.tmp` first (folders made as needed), then swapped in; false on failure. */
fun File.writeSafely(bytes: ByteArray): Boolean = try {
    parentFile?.mkdirs()
    val tmp = File("$path.tmp")
    tmp.writeBytes(bytes)
    tmp.renameTo(this) || (delete() && tmp.renameTo(this))
} catch (e: Exception) {
    false
}
