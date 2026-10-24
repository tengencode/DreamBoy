package com.tengen.gbaemu

import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Save files: written by the emulation thread after the game saves and on pause, loaded again, imported. */
@RunWith(AndroidJUnit4::class)
class SaveTest {
    private val GBA_ERR_SAVE_SIZE = 4
    private lateinit var dir: File
    private lateinit var sav: File

    @Before
    fun freshFolder() {
        dir = File(InstrumentationRegistry.getInstrumentation().targetContext.filesDir, "test_saves")
        dir.deleteRecursively()
        dir.mkdirs()
        sav = File(dir, "game.sav")
    }

    @After
    fun stopSession() = NativeBridge.stop()

    private fun loadSram() = assertEquals(0, NativeBridge.loadRom(TestRoms.rom("jsmolka/sram.gba"), sav.path))

    private fun waitForFile(ms: Long) {
        val until = SystemClock.elapsedRealtime() + ms
        while (!sav.exists() && SystemClock.elapsedRealtime() < until) Thread.sleep(20)
    }

    @Test
    fun aGameThatSavesWritesItsFileSoonAfter() {
        loadSram()
        assertEquals("status", NativeBridge.SAVE_EMPTY, NativeBridge.saveStatus())
        NativeBridge.start()
        waitForFile(1500)
        assertTrue("no save file 1.5 s after start", sav.exists())
        NativeBridge.pause()
        assertArrayEquals(NativeBridge.debugSaveRam(), sav.readBytes())
    }

    @Test
    fun pauseWritesAPendingSaveBeforeItReturns() {
        loadSram()
        NativeBridge.start()
        Thread.sleep(300) // the game has saved, the 1 s wait has not run out
        assertFalse("written too early", sav.exists())
        NativeBridge.pause()
        assertTrue("no save file when pause() returned", sav.exists())
        assertArrayEquals(NativeBridge.debugSaveRam(), sav.readBytes())
    }

    @Test
    fun aFailedWriteIsTriedAgain() {
        val later = File(dir, "later") // e.g. storage full when the game saved; space freed afterwards
        val file = File(later, "game.sav")
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("jsmolka/sram.gba"), file.path))
        NativeBridge.start()
        Thread.sleep(1500) // the game has saved and the write has run: it failed (no folder)
        assertFalse("written without a folder", file.exists())
        later.mkdirs()
        NativeBridge.pause()
        assertTrue("not written once it could be", file.exists())
        assertArrayEquals(NativeBridge.debugSaveRam(), file.readBytes())
    }

    @Test
    fun reopeningTheGameLoadsItsSave() {
        loadSram()
        NativeBridge.start()
        Thread.sleep(300)
        NativeBridge.pause()
        val saved = sav.readBytes()
        NativeBridge.stop()
        loadSram()
        assertEquals("status", NativeBridge.SAVE_LOADED, NativeBridge.saveStatus())
        assertArrayEquals(saved, NativeBridge.debugSaveRam())
    }

    @Test
    fun aSaveFileOfTheWrongSizeIsLeftAlone() {
        val odd = ByteArray(1000) { 7 }
        sav.writeBytes(odd)
        loadSram()
        assertEquals("status", NativeBridge.SAVE_MISFIT, NativeBridge.saveStatus())
        assertArrayEquals(odd, sav.readBytes())
    }

    @Test
    fun aMisfitSaveIsKeptAsideBeforeTheGameSavesOverIt() {
        val odd = ByteArray(65536) { 7 } // e.g. a 64 KiB save for this 32 KiB SRAM game
        sav.writeBytes(odd)
        val aside = File(sav.path + ".misfit")
        loadSram()
        assertEquals("status", NativeBridge.SAVE_MISFIT, NativeBridge.saveStatus())
        assertArrayEquals("kept aside at once", odd, aside.readBytes())
        repeat(2) { // two plays: the first save moves the old file to .bak, the second overwrites .bak
            NativeBridge.start()
            Thread.sleep(300)
            NativeBridge.pause()
            NativeBridge.stop()
            loadSram()
        }
        assertArrayEquals("still there after two saves", odd, aside.readBytes())
    }

    @Test
    fun importAcceptsAFittingSaveAndWritesIt() {
        loadSram()
        val good = ByteArray(32768) { (it * 3).toByte() }
        assertEquals(0, NativeBridge.importSave(good))
        assertEquals("status", NativeBridge.SAVE_LOADED, NativeBridge.saveStatus())
        assertArrayEquals(good, sav.readBytes())
        assertArrayEquals(good, NativeBridge.debugSaveRam())
    }

    @Test
    fun importRejectsASaveThatDoesNotFit() {
        loadSram()
        assertEquals(GBA_ERR_SAVE_SIZE, NativeBridge.importSave(ByteArray(1000)))
        assertEquals("status", NativeBridge.SAVE_EMPTY, NativeBridge.saveStatus())
        assertFalse("a file was written", sav.exists())
    }

    @Test
    fun aGameWithoutASaveChipOffersNothingAndWritesNothing() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba"), sav.path))
        assertEquals("status", NativeBridge.SAVE_NONE, NativeBridge.saveStatus())
        NativeBridge.start()
        Thread.sleep(500)
        NativeBridge.pause()
        assertFalse("a file was written", sav.exists())
    }
}
