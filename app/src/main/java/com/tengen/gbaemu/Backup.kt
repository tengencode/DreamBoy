package com.tengen.gbaemu

import java.io.File
import java.io.FilterOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/**
 * The app's backup file (11, spec §3): a zip of the saves, states, cheats, touch layouts, imported skins, the user's
 * own box art and the three preference files, with `backup.json` saying what it is. [write] makes one; [unpack]
 * checks one and unpacks it. Not the BIOS file, the game list, ROMs or downloaded box art.
 */
object Backup {
    const val VERSION = 1
    const val INFO = "backup.json"
    const val MAX_BYTES = 1L shl 30
    private const val MAX_READ_WHOLE = 1L shl 20 // backup.json, prefs/, boxart/: read into memory, so kept small

    /** The preference files a backup holds, by their names in Android. */
    val PREFS = listOf("settings", "keys", "game_settings")

    /** What a backup says about itself. */
    data class Info(val version: Int, val app: String, val made: Long, val phone: String, val games: Int, val states: Int,
                    val cheats: Int, val skins: Int) {
        fun toJson(): String = "{\"version\": $version, \"app\": ${Json.str(app)}, \"made\": $made, \"phone\": ${Json.str(phone)}, " +
            "\"games\": $games, \"states\": $states, \"cheats\": $cheats, \"skins\": $skins}"

        companion object {
            fun parse(text: String): Info? = try {
                val r = Json.Reader(text)
                val m = r.value() as Map<*, *>
                r.end()
                fun n(key: String) = (m[key] as Double).toLong()
                Info(n("version").toInt(), m["app"] as String, n("made"), m["phone"] as String, n("games").toInt(),
                    n("states").toInt(), n("cheats").toInt(), n("skins").toInt())
            } catch (e: Exception) {
                null
            }
        }
    }

    /** What goes into a backup: the app's files folder, the preference files' values, the user's box-art choices. */
    class Source(val filesDir: File, val prefs: Map<String, Map<String, Int>>, val boxChoices: Map<String, BoxArt.State>)

    /** Not a backup this app can restore. */
    class NotABackup : Exception()

    /** Counts the bytes written through it (the size in the "Backup saved" note). */
    class Counting(out: OutputStream) : FilterOutputStream(out) {
        var count = 0L
            private set

        override fun write(b: Int) {
            out.write(b)
            count++
        }

        override fun write(b: ByteArray, off: Int, len: Int) {
            out.write(b, off, len)
            count += len
        }
    }

    private const val ID = "[0-9a-f]{8}"
    private val PATHS = listOf(
        Regex("backup\\.json"),
        Regex("saves/$ID\\.sav(\\.misfit)?"), // .misfit: a save that didn't fit its game, kept (emu_thread.c)
        Regex("states/$ID/(auto|quick|slot([1-9]|10))\\.(state|png)"),
        Regex("cheats/$ID\\.json"),
        Regex("layout\\.json"),
        Regex("skins/[0-9]+/.+"),
        Regex("boxart/choices\\.json"),
        Regex("boxart/$ID\\.jpg"),
        Regex("prefs/(settings|keys|game_settings)\\.json"),
    )

    /** A path a backup may hold: one of the app's own, with no empty, `.` or `..` part, no leading `/` and no `\`. */
    fun allowed(path: String): Boolean = '\\' !in path &&
        path.split('/').none { it.isEmpty() || it == "." || it == ".." } && PATHS.any { it.matches(path) }

    /** Writes a backup of [src] into [out] (finished, not closed); what it holds. */
    fun write(src: Source, out: OutputStream, app: String, phone: String, now: Long): Info {
        val files = files(src.filesDir, src.boxChoices)
        val games = files.keys.filter { it.startsWith("saves/") || it.startsWith("states/") }.map { it.substringAfter('/').take(8) }.toSet()
        val info = Info(VERSION, app, now, phone, games.size, files.keys.count { it.endsWith(".state") },
            files.keys.count { it.startsWith("cheats/") }, files.keys.filter { it.startsWith("skins/") }.map { it.split('/')[1] }.toSet().size)
        val zip = ZipOutputStream(out)
        fun put(path: String, bytes: ByteArray) {
            zip.putNextEntry(ZipEntry(path))
            zip.write(bytes)
            zip.closeEntry()
        }
        put(INFO, info.toJson().toByteArray())
        for (name in PREFS) put("prefs/$name.json", intsJson(src.prefs[name].orEmpty()).toByteArray())
        put("boxart/choices.json", choicesJson(src.boxChoices).toByteArray())
        for ((path, f) in files) {
            zip.putNextEntry(ZipEntry(path))
            f.inputStream().use { it.copyTo(zip) }
            zip.closeEntry()
        }
        zip.finish()
        return info
    }

    /** The backup's files by path (all but backup.json, the preference files and choices.json), sorted. */
    private fun files(filesDir: File, boxChoices: Map<String, BoxArt.State>): Map<String, File> {
        val out = sortedMapOf<String, File>()
        fun add(path: String, f: File) {
            if (f.isFile && allowed(path)) out[path] = f
        }
        File(filesDir, "saves").listFiles()?.forEach { add("saves/${it.name}", it) }
        File(filesDir, "states").listFiles()?.filter { it.isDirectory }?.forEach { d ->
            d.listFiles()?.forEach { add("states/${d.name}/${it.name}", it) }
        }
        File(filesDir, "cheats").listFiles()?.forEach { add("cheats/${it.name}", it) }
        add("layout.json", File(filesDir, "layout.json"))
        val skins = File(filesDir, "skins")
        skins.listFiles()?.filter { it.isDirectory && it.name.toIntOrNull() != null }?.forEach { d ->
            d.walkTopDown().filter { it.isFile }.forEach { add("skins/" + it.relativeTo(skins).invariantSeparatorsPath, it) }
        }
        for ((id, state) in boxChoices) if (state == BoxArt.State.OWN) add("boxart/$id.jpg", File(filesDir, "boxart/$id.jpg"))
        return out
    }

    /**
     * Checks [input] and unpacks it into [into] (emptied first): only the app's paths, at most [maxBytes] unpacked, a
     * readable `backup.json` of this version or older, readable preference and choice files. Otherwise [NotABackup]
     * and [into] is deleted.
     */
    fun unpack(input: InputStream, into: File, maxBytes: Long = MAX_BYTES): Info {
        into.deleteRecursively()
        into.mkdirs()
        try {
            var total = 0L
            val buffer = ByteArray(64 * 1024)
            ZipInputStream(input.buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    if (entry.isDirectory) continue
                    if (!allowed(entry.name)) throw NotABackup()
                    val cap = if (entry.name == INFO || entry.name.startsWith("prefs/") || entry.name.startsWith("boxart/")) MAX_READ_WHOLE else Long.MAX_VALUE
                    var size = 0L
                    val f = File(into, entry.name)
                    f.parentFile?.mkdirs()
                    f.outputStream().use { o ->
                        while (true) {
                            val n = zip.read(buffer)
                            if (n < 0) break
                            total += n
                            size += n
                            if (total > maxBytes || size > cap) throw NotABackup()
                            o.write(buffer, 0, n)
                        }
                    }
                }
            }
            val info = File(into, INFO).takeIf { it.isFile }?.let { Info.parse(it.readText()) } ?: throw NotABackup()
            if (info.version !in 1..VERSION) throw NotABackup()
            for (name in PREFS) File(into, "prefs/$name.json").takeIf { it.isFile }?.let { readInts(it.readText()) }
            File(into, "boxart/choices.json").takeIf { it.isFile }?.let { readChoices(it.readText()) }
            return info
        } catch (e: Throwable) { // also a StackOverflowError from a deeply nested file
            into.deleteRecursively()
            throw NotABackup()
        }
    }

    /** A preference file's values, `{"name": 1}`; throws on anything else. */
    fun readInts(text: String): Map<String, Int> {
        val r = Json.Reader(text)
        val m = r.value() as Map<*, *>
        r.end()
        return m.entries.associate { (k, v) ->
            val d = v as Double
            require(d == Math.rint(d) && d >= Int.MIN_VALUE && d <= Int.MAX_VALUE)
            k as String to d.toInt()
        }
    }

    /** The box-art choices, `{"<id>": "own" | "removed"}`; throws on anything else. */
    fun readChoices(text: String): Map<String, BoxArt.State> {
        val r = Json.Reader(text)
        val m = r.value() as Map<*, *>
        r.end()
        return m.entries.associate { (k, v) ->
            require(Regex(ID).matches(k as String))
            k to when (v) {
                "own" -> BoxArt.State.OWN
                "removed" -> BoxArt.State.REMOVED
                else -> error("not a choice: $v")
            }
        }
    }

    private fun intsJson(values: Map<String, Int>): String =
        values.toSortedMap().entries.joinToString(", ", "{", "}") { (k, v) -> "${Json.str(k)}: $v" }

    private fun choicesJson(choices: Map<String, BoxArt.State>): String =
        choices.toSortedMap().entries.joinToString(", ", "{", "}") { (k, v) -> "${Json.str(k)}: ${Json.str(v.name.lowercase())}" }

    /** A size for the note: "740 KB", "12.0 MB". */
    fun sizeText(bytes: Long): String =
        if (bytes < 1L shl 20) "${(bytes + 1023) / 1024} KB" else String.format(Locale.US, "%.1f MB", bytes / 1048576.0)
}
