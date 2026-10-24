package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SaveFilesTest {
    private val id = "e86b3224"

    @Test
    fun onlyTheSizesASaveChipCanHave() {
        for (n in listOf(512, 8192, 32768, 65536, 131072)) assertTrue("$n", SaveFiles.validSize(n))
        for (n in listOf(0, 511, 1024, 32767, 131073, 262144)) assertFalse("$n", SaveFiles.validSize(n))
        for (n in listOf(528, 32784, 131088)) assertTrue("$n: mGBA's 16-byte clock footer", SaveFiles.validSize(n))
    }

    @Test
    fun aPickedFileIsReadOnlyUpToTheBiggestSave() {
        val endless = EndlessStream()
        assertNull(SaveFiles.read(endless))
        assertTrue("read ${endless.read} bytes", endless.read <= 131088 + 8192)
        val clockSave = ByteArray(131088) // mGBA's save of a game with a clock
        assertArrayEquals(clockSave, SaveFiles.read(clockSave.inputStream()))
        val save = ByteArray(32768) { it.toByte() }
        assertArrayEquals(save, SaveFiles.read(save.inputStream()))
    }

    @Test
    fun exportGivesTheSaveOrNothing() {
        val dir = Files.createTempDirectory("saves").toFile()
        assertNull(SaveFiles.export(dir, id))
        val bytes = ByteArray(32768) { it.toByte() }
        File(dir, "saves").mkdirs()
        File(dir, "saves/$id.sav").writeBytes(bytes)
        assertArrayEquals(bytes, SaveFiles.export(dir, id))
        dir.deleteRecursively()
    }

    @Test
    fun importKeepsTheOldSaveAndSetsTheAutomaticStateAside() {
        val dir = Files.createTempDirectory("saves").toFile()
        val old = ByteArray(131072) { 1 }
        val new = ByteArray(131072) { 2 }
        File(dir, "saves").mkdirs()
        File(dir, "saves/$id.sav").writeBytes(old)
        val states = File(dir, "states/$id").apply { mkdirs() }
        File(states, "auto.state").writeText("state")
        File(states, "auto.png").writeText("picture")
        File(states, "slot1.state").writeText("slot")
        assertEquals(SaveFiles.Import.OK, SaveFiles.import(dir, id, new))
        assertArrayEquals(new, File(dir, "saves/$id.sav").readBytes())
        assertArrayEquals(old, File(dir, "saves/$id.sav.bak").readBytes())
        assertFalse(File(states, "auto.state").exists())
        assertEquals("state", File(states, "auto.state.old").readText())
        assertEquals("picture", File(states, "auto.png.old").readText())
        assertEquals("slot", File(states, "slot1.state").readText()) // the slots stay
        dir.deleteRecursively()
    }

    @Test
    fun aFirstImportNeedsNoBackup() {
        val dir = Files.createTempDirectory("saves").toFile()
        val new = ByteArray(512) { 9 }
        assertEquals(SaveFiles.Import.OK, SaveFiles.import(dir, id, new))
        assertArrayEquals(new, File(dir, "saves/$id.sav").readBytes())
        assertFalse(File(dir, "saves/$id.sav.bak").exists())
        dir.deleteRecursively()
    }

    @Test
    fun aWrongSizeChangesNothing() {
        val dir = Files.createTempDirectory("saves").toFile()
        val old = ByteArray(32768) { 1 }
        File(dir, "saves").mkdirs()
        File(dir, "saves/$id.sav").writeBytes(old)
        val auto = File(dir, "states/$id/auto.state").apply { parentFile!!.mkdirs(); writeText("state") }
        assertEquals(SaveFiles.Import.WRONG_SIZE, SaveFiles.import(dir, id, ByteArray(1000)))
        assertArrayEquals(old, File(dir, "saves/$id.sav").readBytes())
        assertFalse(File(dir, "saves/$id.sav.bak").exists())
        assertTrue(auto.exists())
        dir.deleteRecursively()
    }
}

/** An endless file (a video picked by mistake): counts what was read. */
class EndlessStream : java.io.InputStream() {
    var read = 0L
    override fun read(): Int = 0.also { read++ }
    override fun read(b: ByteArray, off: Int, len: Int): Int = len.also { read += it }
}
