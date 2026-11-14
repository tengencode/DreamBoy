package com.tengen.gbaemu

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.util.zip.ZipInputStream

/**
 * The imported Delta skins (9), each in `files/skins/<n>/` (n = 1, 2, …; never reused): its `info.json`, its pictures,
 * `thumb.png` (the Skin page's tile picture, made by [thumbnail]) and kept PDF drawings (SkinPicture). An import is
 * unpacked into `skins/.incoming`, checked, then renamed into place, so a failure leaves nothing behind. Plain java.io,
 * so it is tested on the PC.
 */
class SkinFiles(filesDir: File, private val thumbnail: (dir: File, face: DeltaSkin.Face, out: File) -> Unit = { _, _, _ -> },
                private val maxBytes: Long = MAX_BYTES) {
    data class Imported(val number: Int, val dir: File, val skin: DeltaSkin) {
        val thumb: File get() = File(dir, THUMB)
    }

    val root = File(filesDir, "skins")

    /** Every readable imported skin, by number. */
    fun list(): List<Imported> = numbers().sorted().mapNotNull(::get)

    /** Skin [number], or null if it isn't there or can't be read. */
    fun get(number: Int): Imported? {
        val dir = File(root, number.toString())
        val info = File(dir, INFO)
        if (number <= 0 || !info.isFile) return null
        return try {
            Imported(number, dir, DeltaSkin.parse(info.readText()))
        } catch (e: Exception) {
            null
        }
    }

    fun remove(number: Int) {
        File(root, number.toString()).deleteRecursively()
    }

    /** Unpacks, checks and stores a `.deltaskin` (a zip); the same identifier replaces that copy. Throws [SkinException]. */
    fun import(input: InputStream): Imported {
        val incoming = File(root, ".incoming")
        incoming.deleteRecursively()
        try {
            unpack(input, incoming)
            val base = base(incoming) ?: throw SkinException(SkinProblem.NOT_A_SKIN)
            val skin = DeltaSkin.parse(File(base, INFO).readText())
            for (face in listOfNotNull(skin.upright, skin.sideways)) {
                if (!safe(face.image)) throw SkinException(SkinProblem.NOT_A_SKIN)
                if (!File(base, face.image).isFile) throw SkinException(SkinProblem.PICTURE_MISSING)
            }
            try {
                (skin.upright ?: skin.sideways)?.let { thumbnail(base, it, File(base, THUMB)) }
            } catch (e: Exception) { // no tile picture: the tile shows the name only
            }
            val number = list().firstOrNull { it.skin.identifier == skin.identifier }?.number ?: ((numbers().maxOrNull() ?: 0) + 1)
            val target = File(root, number.toString())
            target.deleteRecursively()
            if (!base.renameTo(target)) throw IOException("couldn't move the skin into place")
            return Imported(number, target, skin)
        } catch (e: IOException) { // a damaged zip, a full disk
            throw SkinException(SkinProblem.NOT_A_SKIN)
        } finally {
            incoming.deleteRecursively()
        }
    }

    private fun numbers(): List<Int> = (root.listFiles() ?: emptyArray()).mapNotNull { it.name.toIntOrNull() }.filter { it > 0 }

    private fun unpack(input: InputStream, into: File) {
        var files = 0
        var bytes = 0L
        val buffer = ByteArray(64 * 1024)
        ZipInputStream(input.buffered()).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                val name = entry.name.replace('\\', '/')
                if (!safe(name)) throw SkinException(SkinProblem.NOT_A_SKIN)
                if (entry.isDirectory) continue
                if (++files > MAX_FILES) throw SkinException(SkinProblem.TOO_BIG)
                val out = File(into, name)
                out.parentFile?.mkdirs()
                out.outputStream().use { o ->
                    while (true) {
                        val n = zip.read(buffer)
                        if (n < 0) break
                        bytes += n
                        if (bytes > maxBytes) throw SkinException(SkinProblem.TOO_BIG)
                        o.write(buffer, 0, n)
                    }
                }
            }
        }
        if (files == 0) throw SkinException(SkinProblem.NOT_A_SKIN) // not a zip, or an empty one
    }

    companion object {
        const val INFO = "info.json"
        const val THUMB = "thumb.png"
        const val MAX_FILES = 500
        const val MAX_BYTES = 50L * 1024 * 1024

        /** A path inside the zip that stays inside the skin's folder. */
        fun safe(name: String) = name.isNotEmpty() && !name.startsWith("/") && ':' !in name && name.split('/').none { it == ".." }

        /** Where `info.json` is: at the top, or inside the one top folder (a Mac's `__MACOSX` folder aside). */
        private fun base(dir: File): File? {
            if (File(dir, INFO).isFile) return dir
            val tops = (dir.listFiles() ?: return null).filter { it.isDirectory && it.name != "__MACOSX" }
            return tops.singleOrNull()?.takeIf { File(it, INFO).isFile }
        }
    }
}

/** The imported skin games of console [family] use: the one chosen for it, if it is there and made for that family. */
fun chosenSkin(settings: Settings, family: String, imported: List<SkinFiles.Imported>): SkinFiles.Imported? =
    settings.skinImportFor(family).takeIf { it != 0 }?.let { n -> imported.firstOrNull { it.number == n && it.skin.family == family } }
