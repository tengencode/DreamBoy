package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class BadgeCacheTest {
    private val dir: File = Files.createTempDirectory("badges").toFile()

    @Test
    fun aBadgeIsDownloadedOnceAndKept() {
        var fetched = 0
        val cache = BadgeCache(dir) { fetched++; byteArrayOf(1, 2, 3) }
        val url = "https://media.retroachievements.org/Badge/01001.png"
        assertArrayEquals(byteArrayOf(1, 2, 3), cache.bytes(url))
        assertArrayEquals(byteArrayOf(1, 2, 3), cache.bytes(url))
        assertEquals(1, fetched)
        assertArrayEquals(byteArrayOf(1, 2, 3), File(dir, "01001.png").readBytes())
    }

    @Test
    fun oddNamesAndFailedDownloadsGiveNoPicture() {
        val cache = BadgeCache(dir) { null }
        assertNull(cache.bytes("https://media.retroachievements.org/Badge/01001_lock.png"))
        assertNull(BadgeCache(dir) { byteArrayOf(1) }.bytes("https://x/../secret"))
        assertNull(BadgeCache(dir) { byteArrayOf(1) }.bytes(""))
        assertEquals(0, dir.listFiles()!!.size)
    }

    @Test
    fun prefetchDownloadsWhatIsMissingOnce() {
        val asked = mutableListOf<String>()
        val cache = BadgeCache(dir) { url -> asked += url; byteArrayOf(7) }
        val a = "https://media.retroachievements.org/Badge/01001.png"
        val b = "https://media.retroachievements.org/Badge/01002.png"
        cache.bytes(a) // already kept
        asked.clear()
        assertEquals(1, cache.prefetch(listOf(a, b, b, "")))
        assertEquals(listOf(b), asked)
        assertEquals(0, cache.prefetch(listOf(a, b)))
    }

    @Test
    fun theColourPictureIsTheLockedOnesNameWithoutLock() {
        val locked = RaAchievement(1, 5, false, "T", "D", "", "https://media.retroachievements.org/Badge/12345_lock.png")
        assertEquals("https://media.retroachievements.org/Badge/12345.png", locked.unlockedBadgeUrl)
        val unlocked = locked.copy(unlocked = true, badgeUrl = "https://media.retroachievements.org/Badge/12345.png")
        assertEquals("https://media.retroachievements.org/Badge/12345.png", unlocked.unlockedBadgeUrl)
    }
}
