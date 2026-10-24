package com.tengen.gbaemu

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class BackupTest {
    private val id = "e86b3224"
    private val root = Files.createTempDirectory("backup").toFile()
    private val phone = File(root, "phone")

    private fun put(path: String, text: String) = File(phone, path).apply { parentFile?.mkdirs(); writeText(text) }

    /** A phone with one of everything, and things that must stay out. */
    private fun fill() {
        put("saves/$id.sav", "save")
        put("saves/$id.sav.bak", "old save")
        put("saves/$id.sav.misfit", "a save that didn't fit")
        put("states/$id/auto.state", "auto")
        put("states/$id/auto.png", "auto picture")
        put("states/$id/slot10.state", "ten")
        put("states/$id/auto.state.old", "set aside")
        put("cheats/$id.json", "[]")
        put("layout.json", "{}")
        put("skins/1/info.json", "{}")
        put("skins/1/pictures/a b.png", "picture")
        put("skins/1/cache-p-1080x2424.png", "a PDF skin's drawing, made again when needed")
        put("skins/.incoming/x", "half")
        put("boxart/cccccccc.jpg", "mine")
        put("boxart/aaaaaaaa.jpg", "downloaded")
        put("boxart/boxart.json", "{}")
        put("bios/gba_bios.bin", "nintendo")
        put("library.json", "{}")
    }

    private val source get() = Backup.Source(phone, mapOf("settings" to mapOf("smooth" to 1), "keys" to emptyMap()),
        mapOf("cccccccc" to BoxArt.State.OWN, "dddddddd" to BoxArt.State.REMOVED))

    private fun backup(): ByteArray = ByteArrayOutputStream().also { Backup.write(source, it, "0.1", "Pixel 9", 1_759_600_000_000L) }.toByteArray()

    private fun names(zip: ByteArray): List<String> = ZipInputStream(ByteArrayInputStream(zip)).use { z ->
        generateSequence { z.nextEntry }.map { it.name }.toList()
    }

    private fun zip(vararg entries: Pair<String, String>): ByteArray = ByteArrayOutputStream().also { out ->
        ZipOutputStream(out).use { z -> for ((n, t) in entries) { z.putNextEntry(ZipEntry(n)); z.write(t.toByteArray()); z.closeEntry() } }
    }.toByteArray()

    private val goodInfo = Backup.Info(1, "0.1", 5, "Pixel 9", 1, 1, 0, 0).toJson()

    private fun refused(bytes: ByteArray, max: Long = Backup.MAX_BYTES) {
        val into = File(root, "into")
        try {
            Backup.unpack(ByteArrayInputStream(bytes), into, max)
            fail("expected NotABackup")
        } catch (e: Backup.NotABackup) {
            assertFalse(into.exists())
        }
    }

    @Test
    fun aBackupHoldsExactlyTheAppsParts() {
        fill()
        val zip = backup()
        assertEquals(listOf("backup.json", "prefs/settings.json", "prefs/keys.json", "prefs/game_settings.json", "boxart/choices.json",
            "boxart/cccccccc.jpg", "cheats/$id.json", "layout.json", "saves/$id.sav", "saves/$id.sav.misfit", "skins/1/info.json",
            "skins/1/pictures/a b.png", "states/$id/auto.png", "states/$id/auto.state", "states/$id/slot10.state"), names(zip))
        val info = Backup.unpack(ByteArrayInputStream(zip), File(root, "out"))
        assertEquals(Backup.Info(1, "0.1", 1_759_600_000_000L, "Pixel 9", games = 1, states = 2, cheats = 1, skins = 1), info)
    }

    @Test
    fun theInfoIsWrittenAndRead() {
        val info = Backup.Info(1, "0.1", 9, "One \"Plus\"", 2, 3, 4, 5)
        assertEquals(info, Backup.Info.parse(info.toJson()))
        assertNull(Backup.Info.parse("{\"version\": 1}"))
        assertNull(Backup.Info.parse("nonsense"))
    }

    @Test
    fun onlyTheAppsPathsAreAllowed() {
        for (p in listOf("backup.json", "saves/$id.sav", "states/$id/quick.png", "states/$id/slot1.state", "cheats/$id.json", "layout.json",
            "skins/12/info.json", "skins/1/a/b c.pdf", "boxart/choices.json", "boxart/$id.jpg", "prefs/keys.json")) {
            assertTrue(p, Backup.allowed(p))
        }
        for (p in listOf("../x", "/saves/$id.sav", "saves/../saves/$id.sav", "skins/1/../../x", "skins/a/x", "skins/1/", "skins/1/./x",
            "bios/gba_bios.bin", "library.json", "states/$id/auto.state.old", "states/$id/slot11.state", "saves\\$id.sav",
            "prefs/other.json", "boxart/boxart.json", "ra.json", "ra/badges/01001.png", "ra-fake")) {
            assertFalse(p, Backup.allowed(p))
        }
    }

    @Test
    fun unpackingGivesTheFilesBack() {
        fill()
        val out = File(root, "out")
        Backup.unpack(ByteArrayInputStream(backup()), out)
        assertEquals("ten", File(out, "states/$id/slot10.state").readText())
        assertEquals("picture", File(out, "skins/1/pictures/a b.png").readText())
        assertEquals(mapOf("smooth" to 1), Backup.readInts(File(out, "prefs/settings.json").readText()))
        assertEquals(emptyMap<String, Int>(), Backup.readInts(File(out, "prefs/game_settings.json").readText()))
        assertEquals(mapOf("cccccccc" to BoxArt.State.OWN, "dddddddd" to BoxArt.State.REMOVED),
            Backup.readChoices(File(out, "boxart/choices.json").readText()))
    }

    @Test
    fun unsafeOrForeignFilesAreRefusedAndLeaveNothing() {
        refused(zip("backup.json" to goodInfo, "../evil" to "x"))
        assertFalse(File(root, "evil").exists())
        refused(zip("saves/$id.sav" to "x")) // no backup.json
        refused(zip("backup.json" to goodInfo.replace("\"version\": 1", "\"version\": 2")))
        refused(zip("backup.json" to goodInfo, "prefs/keys.json" to "{\"a\": \"x\"}"))
        refused(zip("backup.json" to goodInfo, "boxart/choices.json" to "{\"$id\": \"found\"}"))
        refused("just text, not a zip".toByteArray())
    }

    @Test
    fun aBackupThatUnpacksTooBigIsRefused() {
        refused(zip("backup.json" to goodInfo, "saves/$id.sav" to "x".repeat(1000)), max = 999)
    }

    @Test
    fun aFileTheAppReadsWholeIsRefusedOverOneMegabyte() { // read into memory: a huge one would crash the restore
        val padded = " ".repeat(1 shl 20) + "{}" // valid JSON, just too big
        refused(zip("backup.json" to goodInfo, "boxart/choices.json" to padded))
        refused(zip("backup.json" to goodInfo, "prefs/keys.json" to padded))
        refused(zip("backup.json" to goodInfo + " ".repeat(1 shl 20)))
    }

    @Test
    fun aDeeplyNestedFileIsRefusedNotACrash() {
        refused(zip("backup.json" to goodInfo, "prefs/keys.json" to "[".repeat(200_000)))
    }

    @Test
    fun settingsAndChoicesAreReadStrictly() {
        assertEquals(mapOf("a" to 1, "b" to -2), Backup.readInts("{\"a\": 1, \"b\": -2}"))
        for (bad in listOf("{\"a\": 1.5}", "{\"a\": true}", "[1]")) {
            try {
                Backup.readInts(bad)
                fail(bad)
            } catch (e: Exception) {
            }
        }
        try {
            Backup.readChoices("{\"../x\": \"own\"}")
            fail("a bad id")
        } catch (e: Exception) {
        }
    }

    @Test
    fun sizesReadNicelyAndAreCounted() {
        assertEquals("1 KB", Backup.sizeText(500))
        assertEquals("2 KB", Backup.sizeText(1536))
        assertEquals("12.0 MB", Backup.sizeText(12L shl 20))
        val c = Backup.Counting(ByteArrayOutputStream())
        c.write(1)
        c.write(ByteArray(10), 2, 5)
        assertEquals(6L, c.count)
    }
}
