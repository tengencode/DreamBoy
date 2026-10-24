package com.tengen.gbaemu

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class RomFileTest {
    private val game = ByteArray(4096) { (it * 7).toByte() }
    private val other = ByteArray(1000) { (it * 3 + 1).toByte() }

    private fun zip(vararg files: Pair<String, ByteArray>): ByteArray {
        val out = ByteArrayOutputStream()
        ZipOutputStream(out).use { z ->
            for ((name, bytes) in files) {
                z.putNextEntry(ZipEntry(name))
                z.write(bytes)
                z.closeEntry()
            }
        }
        return out.toByteArray()
    }

    private fun read(bytes: ByteArray) = RomFile.read(ByteArrayInputStream(bytes))

    private fun assertNotAGame(bytes: ByteArray, why: String) {
        try {
            read(bytes)
            fail("read: $why")
        } catch (e: RomFile.NotAGame) {
        }
    }

    @Test
    fun aPlainGameIsReadAsItIs() = assertArrayEquals(game, read(game))

    @Test
    fun aZippedGameIsTheFirstGbaFileInside() {
        assertArrayEquals(game, read(zip("readme.txt" to other, "dir/" to ByteArray(0), "Game (E).GBA" to game, "b.gba" to other)))
        assertTrue(RomFile.zipHasGame(ByteArrayInputStream(zip("readme.txt" to other, "g.gba" to game))))
    }

    @Test
    fun aZipWithoutAGameIsRefused() {
        val z = zip("readme.txt" to other, "game.sav" to other)
        assertNotAGame(z, "a zip without a .gba")
        assertFalse(RomFile.zipHasGame(ByteArrayInputStream(z)))
        assertFalse(RomFile.zipHasGame(ByteArrayInputStream(game))) // not a zip at all
    }

    @Test
    fun over32MiBIsRefusedAndExactly32MiBIsRead() {
        assertNotAGame(ByteArray(RomFile.MAX_SIZE + 1), "33 MiB")
        assertNotAGame(zip("big.gba" to ByteArray(RomFile.MAX_SIZE + 1)), "33 MiB zipped")
        assertEquals(RomFile.MAX_SIZE, read(ByteArray(RomFile.MAX_SIZE)).size)
        assertNotAGame(ByteArray(0), "an empty file")
    }
}
