package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BoxArtHelperTest {
    private val sample = "Sample Quest - Green Version (USA, Europe)"
    private val dat = """
        clrmamepro (
        	name "Nintendo - Game Boy Advance"
        )

        game (
        	name "Sample Quest - Green Version (USA, Europe)"
        	serial "BPEE"
        	rom ( name "Sample Quest - Green Version (USA, Europe).gba" size 16777216 crc 1F1C08FB serial "BPEE" )
        )
    """.trimIndent()
    private val sampleBox = BoxArtHelper.BOX_URL + "Sample%20Quest%20-%20Green%20Version%20%28USA%2C%20Europe%29.png"
    private val hack = ByteArray(0x200).also { "BPEE".toByteArray().copyInto(it, 0xAC) } // a hack: the original's code
    private val homebrew = ByteArray(0x200) // no code

    /** Replies by URL (unlisted: missing), and every URL asked for. */
    private class FakeNet(val replies: MutableMap<String, Net.Result>) {
        val asked = mutableListOf<String>()
        fun fetch(url: String, max: Int): Net.Result {
            asked += url
            return replies[url] ?: Net.Missing
        }
    }

    private var clock = 1_000_000_000L
    private val read = mutableListOf<String>() // the keys of the games read
    private fun tmp() = File(Files.createTempDirectory("boxart").toFile(), "boxart")
    private fun helper(dir: File, net: FakeNet, art: BoxArt = BoxArt(dir), roms: Map<String, ByteArray> = emptyMap()) =
        BoxArtHelper(dir, art, net::fetch, { g -> read += g.key; roms[g.key] }, { it }, { clock })
    private fun game(key: String, id: String = "") = GameEntry(key, "content://$key", key, id)
    private fun withList() = FakeNet(mutableMapOf(BoxArtHelper.NAMES_URL to Net.Got(dat.toByteArray()),
        sampleBox to Net.Got(byteArrayOf(9, 9))))

    @Test
    fun theGameListIsDownloadedOnceAndKeptForAMonth() {
        val dir = tmp()
        val net = withList()
        assertEquals(sample, helper(dir, net).names()?.find("1f1c08fb", ""))
        fun downloads() = net.asked.count { it == BoxArtHelper.NAMES_URL }
        assertEquals(1, downloads())
        clock += BoxArtHelper.MONTH - 1
        helper(dir, net).names()
        assertEquals(1, downloads())
        clock += 2
        net.replies[BoxArtHelper.NAMES_URL] = Net.Failed
        val h = helper(dir, net)
        assertNotNull(h.names()) // the old one, as the new one failed
        assertEquals(2, downloads())
        assertEquals(1_000_000_000L, h.listTime)
        assertNull(helper(tmp(), FakeNet(mutableMapOf(BoxArtHelper.NAMES_URL to Net.Failed))).names()) // nothing kept
        assertEquals("Sample%20Quest%20-%20Green%20Version%20%28USA%2C%20Europe%29.png", BoxArtHelper.encode("$sample.png"))
    }

    @Test
    fun aKnownGameGetsItsBoxWithoutReadingTheGame() {
        val dir = tmp()
        val art = BoxArt(dir)
        var pictures = 0
        helper(dir, withList(), art).run(listOf(game("e", "1f1c08fb")), onId = { _, _ -> }, onPicture = { pictures++ })
        assertEquals(BoxArt.Entry(BoxArt.State.FOUND, sample, clock), art.entry("1f1c08fb"))
        assertEquals(listOf<Byte>(9, 9), art.picture("1f1c08fb")!!.readBytes().toList())
        assertEquals(1, pictures)
        assertEquals(emptyList<String>(), read)
    }

    @Test
    fun aHackIsFoundByItsCodeAndItsIdIsPassedOn() {
        val dir = tmp()
        val art = BoxArt(dir)
        val ids = mutableListOf<String>()
        helper(dir, withList(), art, mapOf("h" to hack)).run(listOf(game("h")), onId = { g, id -> ids += "${g.key}=$id" }, onPicture = {})
        val id = GameId.of(hack)
        assertEquals(listOf("h=$id"), ids)
        assertEquals(BoxArt.State.FOUND, art.entry(id)!!.state)
        assertEquals(sample, art.entry(id)!!.name)
    }

    @Test
    fun unknownGamesAndMissingBoxesAreRememberedAndAFailureStopsTheRun() {
        val dir = tmp()
        val art = BoxArt(dir)
        val net = withList()
        net.replies[sampleBox] = Net.Missing
        val games = listOf(game("b", GameId.of(homebrew)), game("e", "1f1c08fb"))
        helper(dir, net, art, mapOf("b" to homebrew)).run(games, { _, _ -> }, {})
        assertEquals(BoxArt.Entry(BoxArt.State.NONE, "", clock), art.entry(GameId.of(homebrew)))
        assertEquals(BoxArt.Entry(BoxArt.State.NONE, sample, clock), art.entry("1f1c08fb")) // known, but no box yet
        val asked = net.asked.size
        helper(dir, net, art, mapOf("b" to homebrew)).run(games, { _, _ -> }, {})
        assertEquals(asked, net.asked.size) // the same list: nothing looked up again

        val dir2 = tmp()
        val art2 = BoxArt(dir2)
        val net2 = withList()
        net2.replies[sampleBox] = Net.Failed
        read.clear()
        helper(dir2, net2, art2, mapOf("h" to hack)).run(listOf(game("e", "1f1c08fb"), game("h")), { _, _ -> }, {})
        assertNull(art2.entry("1f1c08fb")) // nothing written: tried again next time
        assertEquals(emptyList<String>(), read) // the run stopped before the hack
    }

    @Test
    fun stopEndsTheRunBeforeTheNextGame() {
        val dir = tmp()
        val art = BoxArt(dir)
        val h = helper(dir, withList(), art, mapOf("h" to hack))
        h.run(listOf(game("e", "1f1c08fb"), game("h")), onId = { _, _ -> }, onPicture = { h.stop() })
        assertTrue(art.picture("1f1c08fb") != null)
        assertFalse("h" in read)
    }

    @Test
    fun cheatFilesAreListedOnceAndMatchedToTheGame() {
        val dir = tmp()
        val file = "$sample (Code Breaker).cht"
        val listing = """[{"name": "$file", "type": "file"}, {"name": "Other (USA).cht", "type": "file"}]"""
        val net = FakeNet(mutableMapOf(BoxArtHelper.CHEAT_LIST_URL to Net.Got(listing.toByteArray()),
            BoxArtHelper.CHEAT_URL + BoxArtHelper.encode(file) to Net.Got("cheat0_desc = \"x\"".toByteArray())))
        assertEquals(listOf("Code Breaker" to file), helper(dir, net).cheatFiles(sample))
        assertEquals(listOf("Code Breaker" to file), helper(dir, net).cheatFiles(sample))
        assertEquals(1, net.asked.count { it == BoxArtHelper.CHEAT_LIST_URL }) // kept
        assertEquals("cheat0_desc = \"x\"", helper(dir, net).cheatFile(file))
        assertNull(helper(dir, net).cheatFile("Missing (USA).cht"))
        assertNull(helper(tmp(), FakeNet(mutableMapOf(BoxArtHelper.CHEAT_LIST_URL to Net.Failed))).cheatFiles(sample))
    }
}
