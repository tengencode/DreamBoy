package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BiosTest {
    @Test
    fun aPickedFileIsReadOnlyUpToABiosSize() {
        val endless = EndlessStream()
        assertNull(Bios.read(endless))
        assertTrue("read ${endless.read} bytes", endless.read <= Bios.SIZE + 8192)
        val bios = ByteArray(Bios.SIZE) { it.toByte() }
        assertArrayEquals(bios, Bios.read(bios.inputStream()))
    }

    @Test
    fun onlySixteenKilobytesIsABiosAndTheUsualOneIsKnownByItsCrc() {
        assertEquals(Bios.Check.WRONG_SIZE, Bios.check(ByteArray(16383)))
        assertEquals(Bios.Check.WRONG_SIZE, Bios.check(ByteArray(0)))
        assertEquals(Bios.Check.UNUSUAL, Bios.check(ByteArray(16384)))
        val real = File("../tests/bios/gba_bios.bin") // the user's own dump, where it exists (never committed)
        if (real.isFile) assertEquals(Bios.Check.USUAL, Bios.check(real.readBytes()))
    }

    @Test
    fun aBiosIsCopiedIntoTheAppReadBackAndRemoved() {
        val dir = Files.createTempDirectory("bios").toFile()
        assertNull(Bios.load(dir))
        val bytes = ByteArray(16384) { (it * 13).toByte() }
        assertTrue(Bios.install(dir, bytes))
        assertTrue(File(dir, "bios/gba_bios.bin").isFile)
        assertArrayEquals(bytes, Bios.load(dir))
        Bios.remove(dir)
        assertNull(Bios.load(dir))
        assertFalse(Bios.install(dir, ByteArray(100))) // not a BIOS: nothing written
        assertNull(Bios.load(dir))
        dir.deleteRecursively()
    }
}
