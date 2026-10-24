package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LibraryTest {
    private fun found(name: String) = FoundGame("docs/$name", "content://tree/$name", Library.nameOf(name))

    @Test
    fun namesComeFromTheFileName() {
        assertEquals("My Hack v5.7 EN+QoL", Library.nameOf("My Hack v5.7 EN+QoL.gba"))
        assertEquals("Space Hunt", Library.nameOf("Space Hunt.ZIP"))
        assertEquals("silver moon", Library.nameOf("silver moon.GbA"))
        assertEquals("notes.txt", Library.nameOf("notes.txt"))
        assertTrue(Library.isGameFile("a.GBA") && Library.isGameFile("b.zip"))
        assertFalse(Library.isGameFile("c.sav") || Library.isGameFile("gba"))
    }

    @Test
    fun playedGamesComeFirstNewestFirstThenTheRestAToZ() {
        val lib = Library(Files.createTempDirectory("lib").toFile())
        lib.merge(listOf(found("zeta.gba"), found("Arcade War.gba"), found("meteor.zip"), found("b.gba")))
        lib.played(lib.games.first { it.name == "meteor" }.copy(id = "11111111", lastPlayed = 100))
        lib.played(lib.games.first { it.name == "b" }.copy(id = "22222222", lastPlayed = 200))
        assertEquals(listOf("b", "meteor", "Arcade War", "zeta"), lib.rows().map { it.name })
    }

    @Test
    fun theFolderDecidesExceptForGamesFromOutsideIt() {
        val lib = Library(Files.createTempDirectory("lib").toFile())
        lib.merge(listOf(found("a.gba"), found("b.gba")))
        lib.played(lib.games.first { it.name == "a" }.copy(id = "aaaaaaaa", lastPlayed = 5))
        lib.played(GameEntry("other/w.gba", "content://single/w.gba", "w", "wwwwwwww", 7, outside = true))
        lib.merge(listOf(found("a.gba"), found("c.gba"))) // b deleted, c added
        assertEquals(listOf("w", "a", "c"), lib.rows().map { it.name })
        val a = lib.rows().first { it.name == "a" }
        assertEquals("aaaaaaaa", a.id) // what the app learnt is kept
        assertEquals(5L, a.lastPlayed)
        assertTrue(lib.rows().first { it.name == "w" }.outside)
        // an outside game later found in the folder becomes one of the folder's
        lib.merge(listOf(found("a.gba"), found("c.gba"), FoundGame("other/w.gba", "content://tree/w.gba", "w")))
        val w = lib.rows().first { it.name == "w" }
        assertFalse(w.outside)
        assertEquals("content://tree/w.gba", w.uri)
        assertEquals("wwwwwwww", w.id)
        lib.remove("docs/c.gba")
        assertEquals(listOf("w", "a"), lib.rows().map { it.name })
    }

    @Test
    fun anOutsideGameFoundInTheFolderByNameAndSizeBecomesTheFoldersOne() {
        // the same file reached through "Downloads" (another provider, another key) and through the games folder
        val lib = Library(Files.createTempDirectory("lib").toFile())
        lib.played(GameEntry("downloads/msf:51", "content://downloads/51", "MyHack", "e86b3224", 9, outside = true, size = 33554432))
        lib.played(GameEntry("downloads/msf:52", "content://downloads/52", "m7_demo", "aaaaaaaa", 8, outside = true, size = 100))
        lib.merge(listOf(FoundGame("tree/GBA/MyHack.gba", "content://tree/MyHack.gba", "MyHack", 33554432),
            FoundGame("tree/GBA/m7_demo.gba", "content://tree/m7_demo.gba", "m7_demo", 999))) // another size: another game
        val rows = lib.rows()
        assertEquals(listOf("MyHack", "m7_demo", "m7_demo"), rows.map { it.name })
        assertEquals(GameEntry("tree/GBA/MyHack.gba", "content://tree/MyHack.gba", "MyHack", "e86b3224", 9, outside = false, size = 33554432), rows[0])
        assertTrue(rows[1].outside)
        assertFalse(rows[2].outside)
        assertEquals(0L, rows[2].lastPlayed)
    }

    @Test
    fun aTwinFoundAfterTheFolderCopyWasListedJoinsIt() {
        // the folder copy was already listed (never played) when the outside game's size became known
        val lib = Library(Files.createTempDirectory("lib").toFile())
        val copy = FoundGame("tree/GBA/MyHack.gba", "content://tree/MyHack.gba", "MyHack", 33554432)
        lib.merge(listOf(copy))
        lib.played(GameEntry("downloads/msf:51", "content://downloads/51", "MyHack", "e86b3224", 9, outside = true, size = 33554432))
        lib.merge(listOf(copy))
        assertEquals(listOf(GameEntry("tree/GBA/MyHack.gba", "content://tree/MyHack.gba", "MyHack", "e86b3224", 9, outside = false,
            size = 33554432)), lib.rows())
    }

    @Test
    fun theListIsSavedAndReadBack() {
        val dir = Files.createTempDirectory("lib").toFile()
        val lib = Library(dir)
        assertTrue(lib.load()) // no file yet: an empty list
        assertNull(lib.folder)
        lib.folder = "content://com.android.externalstorage.documents/tree/primary%3ADownload"
        lib.merge(listOf(found("Café \"Green\".gba"), found("x.zip")))
        lib.played(GameEntry("k\\2", "content://single/y", "y \n z", "0123abcd", 1790619545123, outside = true))
        assertTrue(lib.save())
        val back = Library(dir)
        assertTrue(back.load())
        assertEquals(lib.folder, back.folder)
        assertEquals(lib.rows(), back.rows())
        dir.deleteRecursively()
    }

    @Test
    fun aDamagedFileIsKeptAsBadAndTheListStartsEmpty() {
        val dir = Files.createTempDirectory("lib").toFile()
        File(dir, "library.json").writeText("{\"folder\": \"x\", \"games\": [{\"key\": ")
        val lib = Library(dir)
        assertFalse(lib.load())
        assertTrue(File(dir, "library.json.bad").isFile)
        assertFalse(File(dir, "library.json").exists())
        assertNull(lib.folder)
        assertEquals(emptyList<GameEntry>(), lib.rows())
        dir.deleteRecursively()
    }

    @Test
    fun theContinueCardIsTheMostRecentlyPlayedGame() {
        val a = GameEntry("a", "u", "A", lastPlayed = 100)
        val b = GameEntry("b", "u", "B", lastPlayed = 300)
        val c = GameEntry("c", "u", "C")
        val (last, rest) = Library.continueAndRest(listOf(b, a, c))
        assertEquals(b, last)
        assertEquals(listOf(a, c), rest) // the rest in the list's order, without it
    }

    @Test
    fun noContinueCardWhenNothingWasPlayed() {
        val rows = listOf(GameEntry("a", "u", "A"), GameEntry("b", "u", "B"))
        val (last, rest) = Library.continueAndRest(rows)
        assertNull(last)
        assertEquals(rows, rest)
    }
}
