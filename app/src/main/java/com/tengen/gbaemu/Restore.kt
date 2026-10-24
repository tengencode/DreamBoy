package com.tengen.gbaemu

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream

/**
 * Backing up and restoring the phone's data (11, spec §4): [check] unpacks a backup into `files/restore/incoming`,
 * [apply] writes a safety copy (`files/restore/safety.zip`) and then replaces each part; if a step fails, the safety
 * copy is put back the same way. [undo] puts the safety copy back once. [prefs]: the three preference files by name.
 * [beforePart] is called before each part is replaced (tests make it fail).
 */
class Restore(private val filesDir: File, private val prefs: Map<String, IntStore>, private val boxArt: BoxArt,
              private val beforePart: (String) -> Unit = {}) {
    private val work = File(filesDir, "restore")
    private val incoming = File(work, "incoming")
    val safety = File(work, "safety.zip")

    /** [biosOff]: the backup had the BIOS file switched on, but this phone has no BIOS file, so it was switched off. */
    class Result(val ok: Boolean, val biosOff: Boolean)

    fun source() = Backup.Source(filesDir, Backup.PREFS.associateWith { prefs[it]?.all().orEmpty() }, boxArt.choices())

    /** A backup of the phone's data into [out]; its size in bytes. Never at the same time as a restore or an undo. */
    @Synchronized
    fun backUp(out: OutputStream, app: String, phone: String, now: Long): Long {
        val counting = Backup.Counting(out)
        Backup.write(source(), counting, app, phone, now)
        counting.flush()
        return counting.count
    }

    /** Checks and unpacks [input] for [apply]; null: not a backup this app can restore (nothing changed). */
    fun check(input: InputStream): Backup.Info? = try {
        Backup.unpack(input, incoming)
    } catch (e: Backup.NotABackup) {
        null
    }

    /** The checked backup is dropped (the user said Cancel). */
    fun discard() {
        incoming.deleteRecursively()
    }

    /** The checked backup replaces the phone's data, after a safety copy. */
    @Synchronized
    fun apply(app: String, phone: String, now: Long): Result {
        if (!incoming.isDirectory) return Result(false, false)
        try {
            val tmp = File(work, "safety.zip.tmp")
            tmp.outputStream().use { backUp(it, app, phone, now) }
            safety.delete()
            if (!tmp.renameTo(safety)) throw IOException("couldn't keep the safety copy")
        } catch (e: Exception) {
            discard()
            return Result(false, false)
        }
        return swapOrRollBack()
    }

    /** Puts the safety copy back (Undo restore); then it is deleted. */
    @Synchronized
    fun undo(): Result {
        if (!safety.isFile || safety.inputStream().use { check(it) } == null) return Result(false, false)
        val r = swapOrRollBack()
        if (r.ok) safety.delete()
        return r
    }

    private fun swapOrRollBack(): Result = try {
        Result(true, swap(incoming, beforePart))
    } catch (e: Throwable) { // an Error half-way must still put the phone back
        try {
            val back = File(work, "rollback")
            safety.inputStream().use { Backup.unpack(it, back) }
            swap(back) {}
            back.deleteRecursively()
        } catch (x: Exception) { // nothing more can be done
        }
        Result(false, false)
    } finally {
        discard()
    }

    /** Replaces each part with [from]'s; true if the BIOS file setting had to be switched off. */
    private fun swap(from: File, before: (String) -> Unit): Boolean {
        for (part in listOf("saves", "states", "cheats", "skins")) {
            before(part)
            val target = File(filesDir, part)
            target.deleteRecursively()
            File(from, part).takeIf { it.isDirectory }?.let { move(it, target) }
        }
        before("layout.json")
        val layout = File(filesDir, "layout.json")
        layout.delete()
        File(from, "layout.json").takeIf { it.isFile }?.let { move(it, layout) }
        before("boxart")
        val choices = File(from, "boxart/choices.json").takeIf { it.isFile }?.let { Backup.readChoices(it.readText()) }.orEmpty()
        val pictures = choices.keys.mapNotNull { id -> File(from, "boxart/$id.jpg").takeIf { it.isFile }?.let { id to it.readBytes() } }.toMap()
        boxArt.replaceChoices(choices, pictures)
        before("prefs")
        for (name in Backup.PREFS) {
            val store = prefs[name] ?: continue
            store.clear()
            File(from, "prefs/$name.json").takeIf { it.isFile }?.let { Backup.readInts(it.readText()) }?.forEach { (k, v) -> store.put(k, v) }
        }
        val settings = prefs["settings"]
        if (settings?.get("bios_file") == 1 && !Bios.file(filesDir).isFile) {
            settings.put("bios_file", 0)
            return true
        }
        return false
    }

    private fun move(from: File, to: File) {
        to.parentFile?.mkdirs()
        if (!from.renameTo(to)) {
            from.copyRecursively(to, overwrite = true)
            from.deleteRecursively()
        }
    }
}
