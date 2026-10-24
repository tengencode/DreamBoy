package com.tengen.gbaemu

import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The library on the phone: a game read from a zip runs; a save exported and imported comes back byte for byte. */
@RunWith(AndroidJUnit4::class)
class LibraryDeviceTest {
    private val dir = File(InstrumentationRegistry.getInstrumentation().targetContext.filesDir, "test_library")

    @After
    fun stopSession() {
        NativeBridge.stop()
        dir.deleteRecursively()
    }

    private fun bytesOf(romPath: String): ByteArray = TestRoms.rom(romPath).let { b -> ByteArray(b.remaining()).also { b.get(it) } }

    private fun direct(bytes: ByteArray): ByteBuffer = ByteBuffer.allocateDirect(bytes.size).put(bytes).also { it.flip() }

    @Test
    fun aZippedGameRunsWithTheIdOfTheGameInside() {
        val game = bytesOf("tonc/m7_demo.gba")
        val zip = ByteArrayOutputStream().also { out ->
            ZipOutputStream(out).use { z ->
                z.putNextEntry(ZipEntry("readme.txt")); z.write("hello".toByteArray()); z.closeEntry()
                z.putNextEntry(ZipEntry("m7_demo.gba")); z.write(game); z.closeEntry()
            }
        }.toByteArray()
        val read = RomFile.read(ByteArrayInputStream(zip))
        assertArrayEquals(game, read)
        assertEquals(GameId.of(game), GameId.of(read))
        assertEquals(0, NativeBridge.loadRom(direct(read)))
        NativeBridge.start()
        Thread.sleep(300)
        assertTrue("no frames ran", NativeBridge.debugFrameCount() > 0)
    }

    @Test
    fun anExportedSaveImportsBackExactly() {
        val game = bytesOf("jsmolka/sram.gba")
        val id = GameId.of(game)
        val sav = SaveFiles.file(dir, id).apply { parentFile!!.mkdirs() }
        assertEquals(0, NativeBridge.loadRom(direct(game), sav.path))
        NativeBridge.start()
        val until = SystemClock.elapsedRealtime() + 2000
        while (!sav.exists() && SystemClock.elapsedRealtime() < until) Thread.sleep(20)
        NativeBridge.pause()
        NativeBridge.stop()
        val exported = SaveFiles.export(dir, id)
        assertNotNull("the game wrote no save", exported)
        val other = File(dir, "other")
        assertEquals(SaveFiles.Import.OK, SaveFiles.import(other, id, exported!!))
        assertArrayEquals(exported, SaveFiles.file(other, id).readBytes())
        // and the game takes it: loaded again with the imported file, its save RAM is those bytes
        assertEquals(0, NativeBridge.loadRom(direct(game), SaveFiles.file(other, id).path))
        assertArrayEquals(exported, NativeBridge.debugSaveRam())
    }
}
