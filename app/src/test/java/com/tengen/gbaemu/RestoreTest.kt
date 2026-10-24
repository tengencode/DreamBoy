package com.tengen.gbaemu

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RestoreTest {
    private val id = "e86b3224"
    private val other = "1f1c08fb"

    private inner class Phone(name: String, failAt: String? = null) {
        val dir: File = Files.createTempDirectory(name).toFile()
        val prefs = Backup.PREFS.associateWith { MemoryStore() }
        val art = BoxArt(File(dir, "boxart"))
        val restore = Restore(dir, prefs, art) { part -> if (part == failAt) error("disk trouble at $part") }
        fun put(path: String, text: String) = File(dir, path).apply { parentFile?.mkdirs(); writeText(text) }
        fun backup(): ByteArray = ByteArrayOutputStream().also { restore.backUp(it, "0.1", "Pixel 9", 1000) }.toByteArray()

        /** The parts a backup holds, as path → text. */
        fun tree(): Map<String, String> = dir.walkTopDown().filter { it.isFile }
            .map { it.relativeTo(dir).invariantSeparatorsPath to it.readText() }
            .filter { (p, _) -> listOf("saves/", "states/", "cheats/", "skins/", "layout.json").any { p.startsWith(it) } }
            .toMap().toSortedMap()

        fun settings() = prefs.mapValues { it.value.all() }
    }

    private fun full(p: Phone) {
        p.put("saves/$id.sav", "save")
        p.put("states/$id/auto.state", "auto")
        p.put("states/$id/auto.png", "picture")
        p.put("states/$id/slot1.state", "one")
        p.put("cheats/$id.json", "[]")
        p.put("layout.json", "{\"a\": 1}")
        p.put("skins/1/info.json", "{}")
        p.put("skins/1/sub/p.png", "png")
        p.art.own("cccccccc", byteArrayOf(5))
        p.art.remove("dddddddd")
        p.prefs.getValue("settings").put("smooth", 1)
        p.prefs.getValue("keys").put("k", 42)
        p.prefs.getValue("game_settings").put("$id.save", 3)
    }

    private fun restore(to: Phone, bytes: ByteArray): Restore.Result {
        assertTrue(to.restore.check(ByteArrayInputStream(bytes)) != null)
        return to.restore.apply("0.1", "Pixel 9", 2000)
    }

    /** An automatic backup still writing (it waits on [go]) when the user confirms a restore. */
    private class SlowStream(val started: java.util.concurrent.CountDownLatch, val go: java.util.concurrent.CountDownLatch) :
        java.io.OutputStream() {
        val out = ByteArrayOutputStream()
        override fun write(b: Int) {
            started.countDown()
            go.await()
            out.write(b)
        }
    }

    @Test
    fun aRestoreWaitsForABackupThatIsStillBeingWritten() {
        val a = Phone("a")
        full(a)
        val b = Phone("b")
        full(b)
        b.put("saves/$id.sav", "b's own save")
        assertTrue(b.restore.check(ByteArrayInputStream(a.backup())) != null)
        val started = java.util.concurrent.CountDownLatch(1)
        val go = java.util.concurrent.CountDownLatch(1)
        val slow = SlowStream(started, go)
        val backup = Thread { b.restore.backUp(slow, "0.1", "Pixel 9", 1500) }.apply { start() }
        started.await()
        var restored: Restore.Result? = null
        val restore = Thread { restored = b.restore.apply("0.1", "Pixel 9", 2000) }.apply { start() }
        restore.join(300)
        assertNull("the restore ran while the backup was being written", restored)
        go.countDown()
        backup.join()
        restore.join()
        assertTrue(restored!!.ok)
        val zip = java.util.zip.ZipInputStream(ByteArrayInputStream(slow.out.toByteArray()))
        val save = generateSequence { zip.nextEntry }.first { it.name == "saves/$id.sav" }.let { zip.readBytes().decodeToString() }
        assertEquals("the backup holds the phone as it was, not half-restored", "b's own save", save)
    }

    @Test
    fun backUpThenRestoreOnAnEmptyPhoneGivesEverythingBack() {
        val a = Phone("a")
        full(a)
        val b = Phone("b")
        val info = b.restore.check(ByteArrayInputStream(a.backup()))!!
        assertEquals(listOf(1, 2, 1, 1), listOf(info.games, info.states, info.cheats, info.skins))
        assertTrue(b.restore.apply("0.1", "Pixel 9", 2000).ok)
        assertEquals(a.tree(), b.tree())
        assertEquals(a.settings(), b.settings())
        assertEquals(a.art.choices(), b.art.choices())
        assertEquals(5.toByte(), b.art.picture("cccccccc")!!.readBytes().single())
        assertFalse(File(b.dir, "restore/incoming").exists())
    }

    @Test
    fun restoringReplacesButKeepsDownloadedBoxArt() {
        val a = Phone("a")
        full(a)
        val b = Phone("b")
        b.put("saves/$other.sav", "theirs")
        b.put("states/$other/slot3.state", "theirs")
        b.art.found("aaaaaaaa", "A", byteArrayOf(1), 1)
        b.art.own("eeeeeeee", byteArrayOf(2))
        b.prefs.getValue("settings").put("sound", 0)
        assertTrue(restore(b, a.backup()).ok)
        assertEquals(a.tree(), b.tree()) // the other game's save and state are gone
        assertEquals(mapOf("smooth" to 1), b.prefs.getValue("settings").all())
        assertTrue(b.art.picture("aaaaaaaa")!!.isFile)
        assertNull(b.art.entry("eeeeeeee"))
    }

    @Test
    fun aFailureHalfWayPutsThePhoneBack() {
        val a = Phone("a")
        full(a)
        val b = Phone("b", failAt = "cheats")
        b.put("saves/$other.sav", "theirs")
        b.put("cheats/$other.json", "[1]")
        b.prefs.getValue("keys").put("k", 7)
        val before = b.tree()
        val r = restore(b, a.backup())
        assertFalse(r.ok)
        assertEquals(before, b.tree())
        assertEquals(mapOf("k" to 7), b.prefs.getValue("keys").all())
    }

    @Test
    fun undoPutsBackWhatWasThereOnce() {
        val a = Phone("a")
        full(a)
        val b = Phone("b")
        b.put("saves/$other.sav", "theirs")
        b.prefs.getValue("keys").put("k", 7)
        val before = b.tree()
        assertTrue(restore(b, a.backup()).ok)
        assertTrue(b.restore.safety.isFile)
        assertTrue(b.restore.undo().ok)
        assertEquals(before, b.tree())
        assertEquals(mapOf("k" to 7), b.prefs.getValue("keys").all())
        assertFalse(b.restore.safety.exists())
        assertFalse(b.restore.undo().ok)
    }

    @Test
    fun theBiosSettingIsSwitchedOffWithoutABiosFile() {
        val a = Phone("a")
        a.prefs.getValue("settings").put("bios_file", 1)
        val bytes = a.backup()
        val b = Phone("b")
        assertTrue(restore(b, bytes).biosOff)
        assertEquals(0, b.prefs.getValue("settings").get("bios_file"))
        val c = Phone("c")
        c.put("bios/gba_bios.bin", "x")
        assertFalse(restore(c, bytes).biosOff)
        assertEquals(1, c.prefs.getValue("settings").get("bios_file"))
        assertTrue(File(c.dir, "bios/gba_bios.bin").isFile) // the BIOS file itself is never touched
    }

    @Test
    fun aFileThatIsNotABackupChangesNothing() {
        val b = Phone("b")
        b.put("saves/$other.sav", "theirs")
        assertNull(b.restore.check(ByteArrayInputStream("hello".toByteArray())))
        assertFalse(b.restore.apply("0.1", "Pixel 9", 1).ok) // nothing checked
        assertEquals(mapOf("saves/$other.sav" to "theirs"), b.tree())
        assertFalse(b.restore.safety.exists())
    }

    @Test
    fun theRetroAchievementsSignInIsNeitherBackedUpNorTouchedByARestore() {
        val a = Phone("a")
        full(a)
        a.put("ra.json", "{\"user\": \"A\", \"token\": \"TA\"}")
        a.put("ra/badges/01001.png", "badge")
        val bytes = a.backup()
        val b = Phone("b")
        b.put("ra.json", "{\"user\": \"B\", \"token\": \"TB\"}")
        assertTrue(restore(b, bytes).ok)
        assertEquals("{\"user\": \"B\", \"token\": \"TB\"}", File(b.dir, "ra.json").readText())
        assertFalse(File(b.dir, "ra/badges/01001.png").exists())
    }
}
