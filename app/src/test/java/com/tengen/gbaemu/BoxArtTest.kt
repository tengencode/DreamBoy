package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BoxArtTest {
    private val dir = File(Files.createTempDirectory("boxart").toFile(), "boxart")
    private val jpeg = byteArrayOf(1, 2, 3)

    @Test
    fun aNewGameNeedsALookupAndAFoundOneKeepsItsBox() {
        val art = BoxArt(dir)
        assertTrue(art.needsLookup("1f1c08fb", 0))
        art.found("1f1c08fb", "Sample Quest - Green Version (USA, Europe)", jpeg, 500)
        assertFalse(art.needsLookup("1f1c08fb", 1000))
        assertArrayEquals(jpeg, art.picture("1f1c08fb")!!.readBytes())
        val again = BoxArt(dir) // read back from boxart.json
        assertEquals(BoxArt.Entry(BoxArt.State.FOUND, "Sample Quest - Green Version (USA, Europe)", 500), again.entry("1f1c08fb"))
        assertTrue(again.picture("1f1c08fb")!!.isFile)
    }

    @Test
    fun aGameNotFoundIsLookedUpAgainOnlyAfterANewerList() {
        val art = BoxArt(dir)
        art.none("12345678", "", 100)
        assertNull(art.picture("12345678"))
        assertFalse(art.needsLookup("12345678", 50))
        assertFalse(art.needsLookup("12345678", 100))
        assertTrue(art.needsLookup("12345678", 150))
    }

    @Test
    fun theUsersOwnPictureAndARemovedBoxAreNeverLookedUp() {
        val art = BoxArt(dir)
        art.none("12345678", "Sample Quest - Green Version (USA, Europe)", 100)
        assertTrue(art.own("12345678", jpeg))
        assertEquals(BoxArt.State.OWN, art.entry("12345678")!!.state)
        assertEquals("Sample Quest - Green Version (USA, Europe)", art.entry("12345678")!!.name) // kept for the cheats
        assertFalse(art.needsLookup("12345678", Long.MAX_VALUE))
        art.remove("12345678")
        assertNull(art.picture("12345678"))
        assertFalse(File(dir, "12345678.jpg").exists())
        assertEquals(BoxArt.State.REMOVED, art.entry("12345678")!!.state)
        assertFalse(art.needsLookup("12345678", Long.MAX_VALUE))
        assertTrue(art.own("12345678", jpeg)) // chosen again
        assertTrue(art.picture("12345678")!!.isFile)
    }

    @Test
    fun aFoundGameWhosePictureIsGoneIsLookedUpAgain() {
        val art = BoxArt(dir)
        art.found("1f1c08fb", "Sample Quest - Green Version (USA, Europe)", jpeg, 500)
        File(dir, "1f1c08fb.jpg").delete()
        assertNull(art.picture("1f1c08fb"))
        assertTrue(art.needsLookup("1f1c08fb", 0))
    }

    @Test
    fun aDamagedMemoryFileIsKeptAsideAndEverythingStartsAgain() {
        dir.mkdirs()
        File(dir, "boxart.json").writeText("{ broken")
        val art = BoxArt(dir)
        assertNull(art.entry("1f1c08fb"))
        assertTrue(File(dir, "boxart.json.bad").isFile)
        art.none("1f1c08fb", "", 1)
        assertEquals(BoxArt.State.NONE, BoxArt(dir).entry("1f1c08fb")!!.state)
    }

    @Test
    fun theChoicesAreTheUsersOwnAndRemovedOnly() {
        val art = BoxArt(dir)
        art.found("aaaaaaaa", "A", jpeg, 1)
        art.none("bbbbbbbb", "", 1)
        art.own("cccccccc", jpeg)
        art.remove("dddddddd")
        assertEquals(mapOf("cccccccc" to BoxArt.State.OWN, "dddddddd" to BoxArt.State.REMOVED), art.choices())
    }

    @Test
    fun replacingTheChoicesKeepsTheDownloadedOnes() {
        val art = BoxArt(dir)
        art.found("aaaaaaaa", "A", jpeg, 1)
        art.own("cccccccc", jpeg)
        art.remove("dddddddd")
        val mine = byteArrayOf(7, 7)
        art.replaceChoices(mapOf("eeeeeeee" to BoxArt.State.OWN, "aaaaaaaa" to BoxArt.State.REMOVED, "ffffffff" to BoxArt.State.OWN),
            mapOf("eeeeeeee" to mine)) // ffffffff: no picture, left out
        val again = BoxArt(dir)
        assertEquals(mapOf("eeeeeeee" to BoxArt.State.OWN, "aaaaaaaa" to BoxArt.State.REMOVED), again.choices())
        assertArrayEquals(mine, again.picture("eeeeeeee")!!.readBytes())
        assertNull(again.picture("aaaaaaaa"))
        assertNull(again.entry("cccccccc")) // the old own choice is gone (looked up again)
        assertFalse(File(dir, "cccccccc.jpg").exists())
        assertNull(again.entry("dddddddd"))
        assertEquals("A", again.entry("aaaaaaaa")!!.name) // what was known is kept
    }
}
