package com.tengen.gbaemu

import java.io.File

/**
 * RetroAchievements' badge pictures kept on the phone (14) in `files/ra/badges/` (left out of backups: they download
 * again), by the name at the end of their URL (e.g. `01001.png`, `01001_lock.png`). [fetch] downloads one (null: failed).
 */
class BadgeCache(private val dir: File, private val fetch: (String) -> ByteArray?) {
    private fun fileOf(url: String): File? =
        url.substringAfterLast('/').substringBefore('?').takeIf(NAME::matches)?.let { File(dir, it) }

    /** The picture's bytes, from the folder or downloaded and kept; null without one. Slow: not on the UI thread. */
    fun bytes(url: String): ByteArray? {
        val f = fileOf(url) ?: return null
        if (f.isFile) return try { f.readBytes() } catch (e: Exception) { null }
        val b = fetch(url) ?: return null
        try {
            dir.mkdirs()
            val tmp = File.createTempFile(f.name, ".tmp", dir) // its own name: a pop-up may fetch it at the same time
            tmp.writeBytes(b)
            if (!tmp.renameTo(f)) tmp.delete()
        } catch (e: Exception) {
        }
        return b
    }

    /**
     * Downloads the pictures not kept yet (a game's badges when it loads, so an unlock pop-up has its picture at once);
     * returns how many it got. Slow: on a background thread.
     */
    fun prefetch(urls: List<String>): Int =
        urls.distinct().count { url -> fileOf(url)?.isFile == false && bytes(url) != null }

    companion object {
        private val NAME = Regex("[A-Za-z0-9_-]{1,64}\\.png")
    }
}
