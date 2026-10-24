package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The settings reaching the C side: the picture modes, sound on/off, rewind, a game's overrides, a BIOS file. */
@RunWith(AndroidJUnit4::class)
class SettingsDeviceTest {
    private val dir = File(InstrumentationRegistry.getInstrumentation().targetContext.filesDir, "test_settings")

    @After
    fun backToDefaults() {
        NativeBridge.stop()
        NativeBridge.setBios(null)
        NativeBridge.setRewindBudget(64L * 1024 * 1024)
        NativeBridge.setSound(true, 2)
        NativeBridge.setPicture(Settings.SIZE_FIT, false)
        dir.deleteRecursively()
    }

    private val black = 0xFF shl 24
    private val white = -1 // 0xFFFFFFFF

    /** Black and white columns: sharp keeps only those two, smooth mixes them at the edges. */
    private val stripes = ShortArray(240 * 160) { i -> if ((i % 240) % 2 == 0) 0x7FFF else 0 }

    @Test
    fun smoothPixelsMixNeighboursAndSharpOnesDoNot() {
        val sharp = NativeBridge.debugDrawFrameWith(stripes, 720, 480, Settings.SIZE_FIT, false)
        val smooth = NativeBridge.debugDrawFrameWith(stripes, 720, 480, Settings.SIZE_FIT, true)
        assertNotNull("offscreen OpenGL ES 3 drawing failed", sharp)
        assertTrue("sharp: only black and white", sharp!!.all { it == black || it == white })
        assertTrue("smooth: greys between the columns", smooth!!.any { it != black && it != white })
    }

    @Test
    fun theSizeModesPlaceThePicture() {
        val allWhite = ShortArray(240 * 160) { 0x7FFF }
        val whole = NativeBridge.debugDrawFrameWith(allWhite, 1080, 720, Settings.SIZE_INTEGER, false)!!
        assertEquals("left of 4x (960 wide, from x 60)", black, whole[360 * 1080 + 59])
        assertEquals("inside 4x", white, whole[360 * 1080 + 60])
        assertEquals("above 4x (640 high, from y 40)", black, whole[39 * 1080 + 540])
        val stretch = NativeBridge.debugDrawFrameWith(allWhite, 700, 200, Settings.SIZE_STRETCH, false)!!
        assertTrue("stretch fills everything", stretch.all { it == white })
        val fit = NativeBridge.debugDrawFrameWith(allWhite, 700, 200, Settings.SIZE_FIT, false)!!
        assertEquals("fit leaves bars", black, fit[100 * 700 + 10])
    }

    @Test
    fun soundCanBeSwitchedOffAndOnWhileTheGameRuns() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/snd1_demo.gba")))
        NativeBridge.start()
        Thread.sleep(1000)
        assertEquals("sound at start", 1L, NativeBridge.debugAudioStats()[0])
        NativeBridge.setSound(false, 2)
        Thread.sleep(500)
        assertEquals("sound off", 0L, NativeBridge.debugAudioStats()[0])
        val frames = NativeBridge.debugFrameCount()
        Thread.sleep(500)
        assertTrue("the clock paces the game without sound", NativeBridge.debugFrameCount() > frames + 20)
        NativeBridge.setSound(true, 8)
        Thread.sleep(1000)
        assertEquals("sound on again", 1L, NativeBridge.debugAudioStats()[0])
    }

    @Test
    fun rewindOffKeepsNoSnapshots() {
        NativeBridge.setRewindBudget(0)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(500)
        assertEquals(0L, NativeBridge.debugRewindStats()[0])
        NativeBridge.setRewindBudget(64L * 1024 * 1024)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(500)
        assertTrue("rewind on again for the next game", NativeBridge.debugRewindStats()[0] > 0)
    }

    @Test
    fun aGamesOverridesApplyToTheNextLoadOnly() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        val detected = NativeBridge.cartSetup()
        assertEquals("the demo has no clock", 0, detected[1] and NativeBridge.HW_RTC)
        NativeBridge.setGameOverrides(GameSettings.SAVE_SRAM, GameSettings.CLOCK_ON)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        val forced = NativeBridge.cartSetup()
        assertEquals(GameSettings.SAVE_SRAM, forced[0])
        assertEquals(NativeBridge.HW_RTC, forced[1] and NativeBridge.HW_RTC)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertEquals("used once", detected.toList(), NativeBridge.cartSetup().toList())
    }

    @Test
    fun aBiosFileIsUsedAndItsStatesDoNotLoadWithTheBuiltInOne() {
        assertEquals(NativeBridge.ERR_BIOS_SIZE, NativeBridge.setBios(ByteArray(100)))
        // 16 KiB of "mov pc, #0x08000000": a BIOS that starts the cartridge at once
        val bios = ByteArray(Bios.SIZE) { i -> byteArrayOf(0x02, 0xF3.toByte(), 0xA0.toByte(), 0xE3.toByte())[i % 4] }
        assertEquals(0, NativeBridge.setBios(bios))
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        dir.mkdirs()
        val withFile = File(dir, "file.state").path
        assertTrue(NativeBridge.saveState(withFile))
        NativeBridge.setBios(null)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertEquals("a BIOS-file state into the built-in BIOS", NativeBridge.ERR_STATE, NativeBridge.loadState(withFile))
        val builtIn = File(dir, "builtin.state").path
        assertTrue(NativeBridge.saveState(builtIn))
        assertEquals(0, NativeBridge.loadState(builtIn))
        assertEquals(0, NativeBridge.setBios(bios))
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertNotEquals("a built-in state into a BIOS file", 0, NativeBridge.loadState(builtIn))
        assertEquals(0, NativeBridge.loadState(withFile))
    }
}
