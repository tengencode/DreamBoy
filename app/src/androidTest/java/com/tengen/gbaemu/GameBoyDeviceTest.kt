package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.nio.ByteBuffer

/** Game Boy and Game Boy Color games in the app's native layer (milestone 17b), with the project's own demo ROMs. */
@RunWith(AndroidJUnit4::class)
class GameBoyDeviceTest {
    private lateinit var dir: File

    @Before
    fun freshFolder() {
        dir = File(InstrumentationRegistry.getInstrumentation().targetContext.filesDir, "test_gb")
        dir.deleteRecursively()
        dir.mkdirs()
    }

    @After
    fun stopSession() = NativeBridge.stop()

    @Test
    fun withoutAGameThePictureIsAGbaOne() {
        NativeBridge.stop()
        assertEquals(-1, NativeBridge.console())
        assertEquals(240, NativeBridge.frameWidth())
        assertEquals(160, NativeBridge.frameHeight())
    }

    @Test
    fun aGameBoyGameAndAColorGameLoadWithTheirOwnPicture() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("../own_roms/gb_demo.gb")))
        assertEquals(NativeBridge.CONSOLE_GB, NativeBridge.console())
        assertEquals(160, NativeBridge.frameWidth())
        assertEquals(144, NativeBridge.frameHeight())
        assertEquals(160 * 144, NativeBridge.frameBgr555()!!.size)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("../own_roms/gbc_demo.gbc")))
        assertEquals(NativeBridge.CONSOLE_GBC, NativeBridge.console())
        assertEquals(160 * 144, NativeBridge.frameBgr555()!!.size)
    }

    /** Review focus: the next game's shape at once — no stale frame of the last console. */
    @Test
    fun aGbaGameAfterAGameBoyGameHasItsOwnPictureAgain() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("../own_roms/gb_demo.gb")))
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertEquals(NativeBridge.CONSOLE_GBA, NativeBridge.console())
        assertEquals(240, NativeBridge.frameWidth())
        assertEquals(240 * 160, NativeBridge.frameBgr555()!!.size)
    }

    @Test
    fun theCallerCanSayWhichConsoleAFileIsFor() {
        // A Game Boy game whose header checksum is wrong (homebrew, hacks) is known by its file's name only.
        val rom = TestRoms.rom("../own_roms/gb_demo.gb")
        rom.put(0x14D, (rom.get(0x14D) + 1).toByte())
        assertEquals(0, NativeBridge.loadGame(rom, null, NativeBridge.CONSOLE_GB))
        assertEquals(NativeBridge.CONSOLE_GB, NativeBridge.console())
    }

    @Test
    fun anUnsupportedCartridgeIsRefusedWithItsOwnError() {
        val rom = TestRoms.rom("../own_roms/gb_demo.gb")
        rom.put(0x147, 0x20) // a chip this app does not have (MBC6)
        var sum = 0
        for (i in 0x134..0x14C) sum = sum - rom.get(i) - 1
        rom.put(0x14D, sum.toByte())
        assertEquals(NativeBridge.ERR_UNSUPPORTED, NativeBridge.loadRom(rom))
        assertEquals(-1, NativeBridge.console())
    }

    @Test
    fun aGameBoyGameRunsAtSpeedWithSound() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("../own_roms/gb_demo.gb")))
        NativeBridge.start()
        Thread.sleep(3000)
        NativeBridge.pause()
        val frames = NativeBridge.debugFrameCount()
        assertTrue("about 179 frames in 3 s, got $frames", frames in 150..200)
        val audio = NativeBridge.debugAudioStats()
        if (audio[0] == 1L) assertTrue("the tone is heard: ${audio[5]} loud frames", audio[5] > 10000)
    }

    @Test
    fun aGameBoyGameSavesToItsFileAndFindsItAgain() {
        val save = File(dir, "demo.sav")
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("../own_roms/gb_demo.gb"), save.path))
        assertEquals(NativeBridge.SAVE_EMPTY, NativeBridge.saveStatus())
        NativeBridge.start()
        Thread.sleep(2500) // the demo counts in its cartridge RAM about once a second
        NativeBridge.stop()
        assertEquals("8 KiB of cartridge RAM", 8192L, save.length())
        val counted = save.readBytes()[0]
        assertTrue("it counted: $counted", counted != 0xFF.toByte())
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("../own_roms/gb_demo.gb"), save.path))
        assertEquals(NativeBridge.SAVE_LOADED, NativeBridge.saveStatus())
        // A save of an odd length loads too (other emulators write them): nothing is refused.
        save.writeBytes(ByteArray(100) { 7 })
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("../own_roms/gb_demo.gb"), save.path))
        assertEquals(NativeBridge.SAVE_LOADED, NativeBridge.saveStatus())
    }

    @Test
    fun aPausedGameBoyGameSavesAndLoadsItsMoment() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("../own_roms/gbc_demo.gbc")))
        NativeBridge.start()
        Thread.sleep(400)
        NativeBridge.pause()
        val state = File(dir, "slot1.state")
        assertTrue("save", NativeBridge.saveState(state.path))
        val saved = NativeBridge.frameBgr555()
        assertNotNull(saved)
        NativeBridge.resume()
        Thread.sleep(400) // the picture scrolls on (and 400 ms is not a whole number of its 8-frame pattern)
        NativeBridge.pause()
        assertFalse("the picture moved on", saved!!.contentEquals(NativeBridge.frameBgr555()!!))
        assertEquals(0, NativeBridge.loadState(state.path))
        assertArrayEquals("back at the saved moment", saved, NativeBridge.frameBgr555())
    }

    /** Review focus: a state of the other console (a restored backup can hold anything) is refused; the game goes on. */
    @Test
    fun aGbaStateIsRefusedByAGameBoyGameAndTheOtherWayRound() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        val gbaState = File(dir, "gba.state")
        assertTrue(NativeBridge.saveState(gbaState.path))
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("../own_roms/gb_demo.gb")))
        val gbState = File(dir, "gb.state")
        assertTrue(NativeBridge.saveState(gbState.path))
        assertEquals(NativeBridge.ERR_STATE, NativeBridge.loadState(gbaState.path))
        NativeBridge.start()
        Thread.sleep(300)
        NativeBridge.pause()
        assertTrue("still running", NativeBridge.debugFrameCount() > 5)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertEquals(NativeBridge.ERR_STATE, NativeBridge.loadState(gbState.path))
    }

    @Test
    fun aGameBoyGameRewinds() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("../own_roms/gb_demo.gb")))
        NativeBridge.start()
        Thread.sleep(1500)
        NativeBridge.setRewinding(true)
        Thread.sleep(500)
        NativeBridge.setRewinding(false)
        NativeBridge.pause()
        assertTrue("steps back were taken: ${NativeBridge.debugRewindStats()[2]}", NativeBridge.debugRewindStats()[2] > 5)
    }

    @Test
    fun cheatsAndSensorsAreNotForGameBoyGames() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("../own_roms/gb_demo.gb")))
        assertEquals(NativeBridge.ERR_CHEAT, NativeBridge.setCheats(arrayOf("02000000:01"), intArrayOf(NativeBridge.CHEAT_RAW)))
        assertEquals(0, NativeBridge.setCheats(arrayOf(), intArrayOf()))
        assertArrayEquals(intArrayOf(0, 0), NativeBridge.cartSetup()) // no save chip to choose, no extras
        NativeBridge.setMotion(1000, 1000, 1000) // ignored, harmless
        NativeBridge.start()
        Thread.sleep(200)
        assertFalse(NativeBridge.rumbleActive())
    }

    @Test
    fun aGameBoyFrameIsDrawnInItsOwnShape() {
        val frame = ShortArray(160 * 144) { 0x001F } // all red
        val w = 320
        val h = 320
        val px = NativeBridge.debugDrawFrame(frame, w, h)!!
        // 10:9 in a square: 320 x 288, 16 rows of black above and below
        assertEquals("black bar", 0xFF000000.toInt(), px[8 * w + 160])
        assertEquals("picture", 0xFF0000FF.toInt(), px[20 * w + 4])
        assertEquals("picture", 0xFF0000FF.toInt(), px[300 * w + 315])
        assertEquals("black bar", 0xFF000000.toInt(), px[310 * w + 160])
    }

    /** 17c: a look colours a grey game from its next frame on, for this game and the ones opened after; never a Color game. */
    @Test
    fun aLookColoursAGreyGameAndLeavesAColorGameAlone() {
        fun framesAfter(look: Int, rom: String): Set<Short> {
            NativeBridge.setLook(look)
            assertEquals(0, NativeBridge.loadRom(TestRoms.rom(rom)))
            NativeBridge.start()
            val from = NativeBridge.debugFrameCount()
            while (NativeBridge.debugFrameCount() < from + 10) Thread.sleep(5)
            NativeBridge.pause()
            return NativeBridge.frameBgr555()!!.toSet()
        }
        try {
            val greys = setOf<Short>(0x7FFF, 0x56B5, 0x294A, 0)
            val grey = framesAfter(Looks.AUTO, "../own_roms/gb_demo.gb") // a made-up game: the console doesn't know it
            assertTrue(grey.isNotEmpty() && greys.containsAll(grey))
            val green = NativeBridge.lookColours(1).take(4).toSet()
            assertEquals(12, NativeBridge.lookColours(1).size)
            assertTrue(green.intersect(greys).isEmpty())
            val shown = framesAfter(1, "../own_roms/gb_demo.gb")
            assertTrue(shown.isNotEmpty() && green.containsAll(shown))
            // While it runs: back to grey from the next frame.
            NativeBridge.setLook(0)
            NativeBridge.resume()
            val from = NativeBridge.debugFrameCount()
            while (NativeBridge.debugFrameCount() < from + 10) Thread.sleep(5)
            NativeBridge.pause()
            assertTrue(greys.containsAll(NativeBridge.frameBgr555()!!.toSet()))
            assertEquals(framesAfter(0, "../own_roms/gbc_demo.gbc"), framesAfter(1, "../own_roms/gbc_demo.gbc"))
        } finally {
            NativeBridge.setLook(0)
        }
    }

    /** 17c: cheats are read and set as the loaded game's console has them. */
    @Test
    fun cheatsAreReadAsTheLoadedGamesConsoleReadsThem() {
        val genie = "00A-17B-C49"
        val shark = "01AA23C1"
        val gbaRaw = "02000000:12"
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("../own_roms/gb_demo.gb")))
        assertArrayEquals(intArrayOf(1, 0), NativeBridge.checkCheat(genie, NativeBridge.CHEAT_AUTO)) // 1 = Game Genie
        assertArrayEquals(intArrayOf(2, 0), NativeBridge.checkCheat(shark, NativeBridge.CHEAT_AUTO)) // 2 = GameShark
        assertEquals(1, NativeBridge.checkCheat(gbaRaw, NativeBridge.CHEAT_AUTO)[1])
        assertEquals(0, NativeBridge.setCheats(arrayOf(genie, shark), intArrayOf(0, 0)))
        assertEquals(NativeBridge.ERR_CHEAT, NativeBridge.setCheats(arrayOf(gbaRaw), intArrayOf(0)))
        assertEquals(0, NativeBridge.setCheats(arrayOf(shark), intArrayOf(2)))
        NativeBridge.start() // a frame with the cheat on, a state saved and loaded with it on
        val from = NativeBridge.debugFrameCount()
        while (NativeBridge.debugFrameCount() < from + 5) Thread.sleep(5)
        NativeBridge.pause()
        assertEquals(0, NativeBridge.setCheats(emptyArray(), IntArray(0)))
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertEquals(1, NativeBridge.checkCheat(genie, NativeBridge.CHEAT_AUTO)[1]) // no GBA code
        assertEquals(0, NativeBridge.checkCheat(gbaRaw, NativeBridge.CHEAT_AUTO)[1])
    }
}
