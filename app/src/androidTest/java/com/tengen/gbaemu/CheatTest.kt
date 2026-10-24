package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test
import org.junit.runner.RunWith

/** Cheats on the phone: set while paused, they change the game's memory and ROM; off undoes the ROM patch. */
@RunWith(AndroidJUnit4::class)
class CheatTest {
    @After
    fun stopSession() = NativeBridge.stop()

    @Test
    fun aCheatSetBeforeStartWritesEveryFrame() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertEquals(0, NativeBridge.setCheats(arrayOf("0203FFF0:12345678"), intArrayOf(NativeBridge.CHEAT_RAW)))
        NativeBridge.start()
        Thread.sleep(300)
        NativeBridge.pause()
        assertEquals(0x12345678, NativeBridge.debugPeek32(0x0203FFF0))
    }

    @Test
    fun romPatchesComeAndGoWithTheCheat() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        val before = NativeBridge.debugPeek32(0x08000100)
        assertEquals(0, NativeBridge.setCheats(arrayOf("08000100:BEEF\n08000102:F00D"), intArrayOf(NativeBridge.CHEAT_AUTO)))
        assertEquals(0xF00DBEEF.toInt(), NativeBridge.debugPeek32(0x08000100))
        assertEquals(0, NativeBridge.setCheats(arrayOf(), intArrayOf()))
        assertEquals(before, NativeBridge.debugPeek32(0x08000100))
    }

    @Test
    fun aRunningGameRefusesCheatsAndABadTextSetsNone() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertEquals(NativeBridge.ERR_CHEAT, NativeBridge.setCheats(arrayOf("not a code"), intArrayOf(NativeBridge.CHEAT_AUTO)))
        NativeBridge.start()
        Thread.sleep(200)
        assertEquals(NativeBridge.ERR_CHEAT, NativeBridge.setCheats(arrayOf("0203FFF0:11"), intArrayOf(NativeBridge.CHEAT_RAW)))
        NativeBridge.pause()
        assertNotEquals(0x11, NativeBridge.debugPeek32(0x0203FFF0) and 0xFF)
    }

    @Test
    fun checkCheatFindsTheTypeOrTheBadLine() {
        assertArrayEquals(intArrayOf(NativeBridge.CHEAT_CODEBREAKER, 0),
            NativeBridge.checkCheat("820257BC 423F\n820257BE 000F", NativeBridge.CHEAT_AUTO))
        assertArrayEquals(intArrayOf(NativeBridge.CHEAT_ACTION_REPLAY, 0),
            NativeBridge.checkCheat("7881A409 E2026E0C\n8E883EFF 92E9660D", NativeBridge.CHEAT_AUTO))
        assertEquals(3, NativeBridge.checkCheat("820257BC 423F\n\nnope", NativeBridge.CHEAT_AUTO)[1])
    }
}
