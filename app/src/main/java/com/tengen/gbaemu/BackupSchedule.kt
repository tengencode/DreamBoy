package com.tengen.gbaemu

import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Automatic backups and the Backup row's time (11), in `files/backup-state.json` (not part of a backup: the folder's
 * permission is this phone's): the picked [folder] (null: automatic backup off), when the last automatic backup and
 * the last backup of any kind were made; when one is due, and which of the folder's files to delete.
 */
class BackupSchedule(private val file: File) {
    var folder: String? = null
    var lastAuto = 0L
    var lastBackup = 0L

    init {
        try {
            val m = Json.Reader(file.readText()).value() as Map<*, *>
            folder = m["folder"] as String?
            lastAuto = (m["lastAuto"] as Double).toLong()
            lastBackup = (m["lastBackup"] as Double).toLong()
        } catch (e: Exception) { // none yet, or damaged: as new
            folder = null
            lastAuto = 0
            lastBackup = 0
        }
    }

    fun save() { // safely: a damaged file would switch automatic backups off without a word
        file.writeSafely("{\"folder\": ${folder?.let(Json::str) ?: "null"}, \"lastAuto\": $lastAuto, \"lastBackup\": $lastBackup}".toByteArray())
    }

    /** An automatic backup is due: a folder is picked and the last one is [DAY] or more ago. */
    fun due(now: Long): Boolean = folder != null && now - lastAuto >= DAY

    companion object {
        const val DAY = 24L * 60 * 60 * 1000
        const val KEEP = 5

        /** The name of a backup made at [now]: `DreamBoy backup 2026-10-04 14-02.zip`. */
        fun fileName(now: Long): String =
            "DreamBoy backup " + SimpleDateFormat("yyyy-MM-dd HH-mm", Locale.US).format(Date(now)) + ".zip"

        /** The app's own backup names (Android adds " (1)", " (2)", … when a name of that minute is taken). */
        val OURS = Regex("DreamBoy backup (\\d{4}-\\d{2}-\\d{2} \\d{2}-\\d{2})( \\((\\d+)\\))?\\.zip")

        /**
         * Of a folder's file names, the app's own beyond the newest [KEEP] (to delete); other files never. Newest = the
         * latest minute in the name, then the highest " (n)" (none = 0), compared as a number — not as text, where
         * " (1)" sorts before ".zip" and "(10)" before "(9)" (found in Claude's check).
         */
        fun toDelete(names: List<String>): List<String> = names.mapNotNull { n -> OURS.matchEntire(n)?.let { n to it } }
            .sortedWith(compareByDescending<Pair<String, MatchResult>> { it.second.groupValues[1] }
                .thenByDescending { it.second.groupValues[3].toLongOrNull() ?: 0L })
            .drop(KEEP).map { it.first }
    }
}
