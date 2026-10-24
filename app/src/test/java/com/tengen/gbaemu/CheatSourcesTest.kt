package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CheatSourcesTest {
    private val files = listOf(
        "Sample Quest - Green Version (USA, Europe) (Code Breaker).cht",
        "Sample Quest - Orange Version (USA, Europe) (Rev 1).cht",
        "Sample Quest - Red Version (USA, Europe) (Rev 1) (Code Breaker).cht",
        "Dungeons _ Dragons - Eye of the Beholder (USA, Europe) (Code Breaker).cht",
        "Example Saga I & II - Two Tales (USA, Australia).cht",
    )

    @Test
    fun githubsListingGivesTheCheatFileNames() {
        val json = """[
            {"name": "007 - Everything or Nothing (World) (Code Breaker).cht", "path": "cht/x", "size": 627, "type": "file",
             "_links": {"self": "https://api.github.com/x"}},
            {"name": "readme.txt", "type": "file", "size": 3},
            {"name": "old", "type": "dir", "size": 0}
        ]"""
        assertEquals(listOf("007 - Everything or Nothing (World) (Code Breaker).cht"), CheatSources.fromListing(json))
        assertNull(CheatSources.fromListing("{\"message\": \"API rate limit exceeded\"}"))
        assertNull(CheatSources.fromListing("<html>"))
    }

    @Test
    fun aGamesFilesAreItsNameOrItsNameAndOneLabel() {
        assertEquals(listOf("Code Breaker" to files[0]), CheatSources.forGame(files, "Sample Quest - Green Version (USA, Europe)"))
        assertEquals(listOf("Cheats" to files[1]), CheatSources.forGame(files, "Sample Quest - Orange Version (USA, Europe) (Rev 1)"))
        assertEquals(emptyList<Pair<String, String>>(), CheatSources.forGame(files, "Sample Quest - Red Version (USA, Europe)"))
        assertEquals(listOf("Code Breaker" to files[2]), CheatSources.forGame(files, "Sample Quest - Red Version (USA, Europe) (Rev 1)"))
    }

    @Test
    fun namesWithSymbolsAreMatchedAsIsOrAsPictureNames() {
        assertEquals(listOf("Code Breaker" to files[3]),
            CheatSources.forGame(files, "Dungeons & Dragons - Eye of the Beholder (USA, Europe)"))
        assertEquals(listOf("Cheats" to files[4]), CheatSources.forGame(files, "Example Saga I & II - Two Tales (USA, Australia)"))
        val two = listOf("Game (USA) (GameShark).cht", "Game (USA) (Code Breaker).cht")
        assertEquals(listOf("Code Breaker", "GameShark"), CheatSources.forGame(two, "Game (USA)").map { it.first }) // A-Z
    }
}
