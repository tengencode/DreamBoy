package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CheatsTest {
    private val sample = listOf(
        Cheat("Walk through walls", NativeBridge.CHEAT_AUTO, "7881A409 E2026E0C\n8E883EFF 92E9660D", true),
        Cheat("Money \"999,999\" \\ \t ünï", NativeBridge.CHEAT_CODEBREAKER, "820257BC 423F\r\n820257BE 000F\n", false),
        Cheat("", NativeBridge.CHEAT_RAW, "", false),
    )

    @Test
    fun theListSurvivesJsonExactly() {
        val json = Cheats.toJson(sample)
        assertEquals(sample, Cheats.fromJson(json))
        assertEquals(emptyList<Cheat>(), Cheats.fromJson(Cheats.toJson(emptyList())))
    }

    @Test
    fun aGamesCheatsAreSavedAndReadBack() {
        val dir = Files.createTempDirectory("cheats").toFile()
        val c = Cheats(dir, "e86b3224")
        assertEquals(emptyList<Cheat>(), c.load()) // no file yet
        c.save(sample)
        assertTrue(File(dir, "cheats/e86b3224.json").isFile)
        assertEquals(sample, Cheats(dir, "e86b3224").load())
        assertEquals(emptyList<Cheat>(), Cheats(dir, "00000000").load()) // another game
        dir.deleteRecursively()
    }

    @Test
    fun aDamagedFileIsKeptAsBadAndReadsAsNothing() {
        val dir = Files.createTempDirectory("cheats").toFile()
        val file = File(dir, "cheats/e86b3224.json").apply { parentFile!!.mkdirs(); writeText("[{\"name\": \"x\", ") }
        assertNull(Cheats(dir, "e86b3224").load())
        assertTrue(File(dir, "cheats/e86b3224.json.bad").isFile)
        assertTrue(!file.exists())
        dir.deleteRecursively()
    }
}
