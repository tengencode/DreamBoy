package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RaPopupsTest {
    private fun ev(kind: Int, id: Int = 1, title: String = "T", progress: String = "") =
        RaEvent(kind, id, 5, title, "D", progress, "https://m/Badge/0$id.png")

    @Test
    fun anEventIsReadFromItsUtf8Parts() {
        val parts = listOf("14", "11", "0", "Première ★", "", "0:10.00", "", "4", "1204", "0:09.50").map { it.toByteArray() }
        assertEquals(RaEvent(RaEvent.LB_SCOREBOARD, 11, 0, "Première ★", "", "0:10.00", "", rank = 4, entries = 1204, best = "0:09.50"),
            RaEvent.of(parts.toTypedArray()))
        assertNull(RaEvent.of(null))
        assertNull(RaEvent.of(arrayOf("x".toByteArray())))
    }

    @Test
    fun theListIsReadSevenPartsAnAchievement() {
        val parts = listOf("1004", "1", "1", "Saver", "Save once", "", "u1", "1002", "10", "0", "Collector", "Collect 10", "4/10", "u2")
            .map { it.toByteArray() }.toTypedArray()
        val list = RaAchievement.list(parts)
        assertEquals(2, list.size)
        assertTrue(list[0].unlocked)
        assertEquals("4/10", list[1].progress)
        assertFalse(list[1].unlocked)
    }

    @Test
    fun cardsShowOneAtATimeForTheirTime() {
        val p = RaPopups()
        p.add(ev(RaEvent.UNLOCKED, 1, "One"), 0)
        p.add(ev(RaEvent.UNLOCKED, 2, "Two"), 0)
        assertEquals("One", p.view().card!!.title)
        assertFalse(p.tick(RaPopups.UNLOCK_MS - 1))
        assertTrue(p.tick(RaPopups.UNLOCK_MS))
        assertEquals("Two", p.view().card!!.title)
        p.tick(2 * RaPopups.UNLOCK_MS)
        assertNull(p.view().card)
    }

    @Test
    fun masteredStaysLongerAndANoteIsShort() {
        val p = RaPopups()
        p.add(RaEvent(RaEvent.MASTERED, 0, 43, "Test Game", "", "", ""), 0)
        p.tick(RaPopups.UNLOCK_MS)
        assertEquals(RaEvent.MASTERED, p.view().card!!.kind)
        p.tick(RaPopups.MASTERED_MS)
        p.note("1 of 5 achievements", RaPopups.MASTERED_MS)
        assertEquals("1 of 5 achievements", p.view().card!!.title)
        p.tick(RaPopups.MASTERED_MS + RaPopups.NOTE_MS)
        assertNull(p.view().card)
    }

    @Test
    fun progressShowsTheLatestForThreeSeconds() {
        val p = RaPopups()
        p.add(ev(RaEvent.PROGRESS, 2, "Collector", "3/10"), 0)
        p.add(ev(RaEvent.PROGRESS, 2, "Collector", "4/10"), 1000)
        assertEquals("4/10", p.view().progress!!.text)
        p.tick(1000 + RaPopups.PROGRESS_MS - 1)
        assertEquals("4/10", p.view().progress!!.text)
        p.tick(1000 + RaPopups.PROGRESS_MS)
        assertNull(p.view().progress)
        p.add(ev(RaEvent.PROGRESS, 2, "Collector", "5/10"), 5000)
        p.add(ev(RaEvent.PROGRESS_HIDE, 0), 5001)
        assertNull(p.view().progress)
    }

    @Test
    fun challengesComeAndGoByAchievement() {
        val p = RaPopups()
        p.add(ev(RaEvent.CHALLENGE_SHOW, 3), 0)
        p.add(ev(RaEvent.CHALLENGE_SHOW, 3), 0)
        p.add(ev(RaEvent.CHALLENGE_SHOW, 4), 0)
        assertEquals(listOf(3, 4), p.view().challenges.map { it.id })
        p.add(ev(RaEvent.CHALLENGE_HIDE, 3), 0)
        assertEquals(listOf(4), p.view().challenges.map { it.id })
        p.clear()
        assertEquals(RaPopups.View(), p.view())
    }

    @Test
    fun aServerErrorAndGoingOfflineAreCards() {
        val p = RaPopups()
        p.add(RaEvent(RaEvent.SERVER_ERROR, 1, 0, "", "The server refused this unlock.", "", ""), 0)
        assertEquals("Couldn't send an unlock", p.view().card!!.title)
        assertEquals("The server refused this unlock.", p.view().card!!.text)
        p.tick(RaPopups.NOTE_MS)
        p.add(RaEvent(RaEvent.OFFLINE, 0, 0, "", "", "", ""), RaPopups.NOTE_MS)
        assertEquals("Offline", p.view().card!!.title)
        p.add(RaEvent(RaEvent.ONLINE, 0, 0, "", "", "", ""), RaPopups.NOTE_MS) // nothing to show
        p.tick(2 * RaPopups.NOTE_MS)
        assertNull(p.view().card)
    }

    @Test
    fun theServersWarningIsANoticeNotAnUnlock() {
        // RetroAchievements adds a made-up achievement (id 101000001 and up) for an app it doesn't know; rcheevos
        // "unlocks" it at once. It is a notice, and it is not one of the game's achievements.
        val p = RaPopups()
        p.add(RaEvent(RaEvent.UNLOCKED, RaPopups.WARNING_ID, 0, "Warning: Unknown Emulator",
            "Hardcore unlocks cannot be earned using this emulator.", "", "https://m/Badge/00000.png"), 0)
        val card = p.view().card!!
        assertEquals(RaPopups.WARNING, card.kind)
        assertEquals("Warning: Unknown Emulator", card.title)
        assertEquals("", card.badgeUrl)
        assertEquals(0, card.points)
        val parts = listOf("101000001", "0", "1", "Warning: Unknown Emulator", "Hardcore unlocks cannot be earned using this emulator.", "", "u0",
            "1004", "5", "0", "Saver", "Save once", "", "u1").map { it.toByteArray() }.toTypedArray()
        val (rows, warning) = RaAchievement.split(RaAchievement.list(parts))
        assertEquals(listOf(1004), rows.map { it.id })
        assertEquals("Hardcore unlocks cannot be earned using this emulator.", warning)
    }

    private fun lb(kind: Int, title: String = "Sprint", progress: String = "", description: String = "", rank: Int = 0,
                   entries: Int = 0, best: String = "", id: Int = 11) =
        RaEvent(kind, id, 0, title, description, progress, "", rank, entries, best)

    @Test
    fun leaderboardNotesAndScoresQueueLikeTheOtherCards() {
        val p = RaPopups()
        p.add(lb(RaEvent.LB_STARTED), 0)
        assertEquals("Sprint", p.view().card!!.title)
        assertEquals("Leaderboard started", p.view().card!!.text)
        p.add(lb(RaEvent.LB_SUBMITTED, progress = "0:10.00"), 0)
        assertFalse(p.tick(RaPopups.BOARD_NOTE_MS - 1)) // a board's note is short
        assertTrue(p.tick(RaPopups.BOARD_NOTE_MS))
        assertEquals("Score 0:10.00 · sending", p.view().card!!.text)
        val t = RaPopups.BOARD_NOTE_MS + 500
        p.add(lb(RaEvent.LB_SCOREBOARD, progress = "0:10.00", rank = 4, entries = 1204, best = "0:09.50"), t)
        val score = p.view().card!!
        assertEquals("Score 0:10.00 · 1,204 players · best 0:09.50", score.text)
        assertEquals(4, score.rank)
        assertFalse(p.tick(t + RaPopups.UNLOCK_MS - 1)) // a score shows as long as an unlock
        assertTrue(p.tick(t + RaPopups.UNLOCK_MS))
    }

    @Test
    fun aBoardsNewsReplacesItsOlderCardSoNothingRunsLate() {
        val p = RaPopups()
        p.add(lb(RaEvent.LB_SUBMITTED, progress = "0:10.00"), 0) // shown
        p.add(lb(RaEvent.LB_STARTED, title = "Survival", id = 12), 0) // waiting
        p.add(lb(RaEvent.LB_SCOREBOARD, progress = "0:10.00", rank = 4, entries = 31), 1000)
        assertEquals("Score 0:10.00 · 31 players", p.view().card!!.text) // the sending card made way at once
        p.add(lb(RaEvent.LB_FAILED, title = "Survival", id = 12), 1000)
        p.tick(1000 + RaPopups.UNLOCK_MS)
        assertEquals("Leaderboard failed", p.view().card!!.text) // took the started note's place
        assertTrue(p.tick(1000 + RaPopups.UNLOCK_MS + RaPopups.BOARD_NOTE_MS))
        assertNull(p.view().card)
    }

    @Test
    fun aBestScoreIsLeftOutWhenItIsThisOne() {
        val p = RaPopups()
        p.add(lb(RaEvent.LB_SCOREBOARD, progress = "0:10.00", rank = 1, entries = 2, best = "0:10.00"), 0)
        assertEquals("Score 0:10.00 · 2 players", p.view().card!!.text)
        assertEquals("Score 7 · 1 player", RaPopups.scoreLine(lb(RaEvent.LB_SCOREBOARD, progress = "7", rank = 1, entries = 1)))
    }

    @Test
    fun aRefusedScoreSaysSo() {
        val p = RaPopups()
        p.add(lb(RaEvent.LB_ERROR, description = "Hardcore unlocks cannot be earned using this emulator."), 0)
        val c = p.view().card!!
        assertEquals(RaEvent.LB_ERROR, c.kind)
        assertEquals("Couldn't send a score", c.title)
        assertEquals("Sprint: Hardcore unlocks cannot be earned using this emulator.", c.text)
    }

    @Test
    fun countersShowUpdateHideAndAtMostThreeShow() {
        val p = RaPopups()
        p.add(lb(RaEvent.TRACKER_SHOW, progress = "0:00.00", id = 1), 0)
        p.add(lb(RaEvent.TRACKER_UPDATE, progress = "0:01.00", id = 1), 0)
        assertEquals(listOf(RaPopups.Tracker(1, "0:01.00")), p.view().trackers)
        for (i in 2..5) p.add(lb(RaEvent.TRACKER_SHOW, progress = "$i", id = i), 0)
        assertEquals(listOf(1, 2, 3), p.view().trackers.map { it.id })
        p.add(lb(RaEvent.TRACKER_HIDE, id = 1), 0)
        assertEquals(listOf(2, 3, 4), p.view().trackers.map { it.id })
        p.clear()
        assertTrue(p.view().trackers.isEmpty())
    }

    @Test
    fun theSwitchHidesNotesAndCountersButNotScores() {
        val p = RaPopups()
        p.leaderboardNotes = false
        p.add(lb(RaEvent.LB_STARTED), 0)
        p.add(lb(RaEvent.LB_FAILED), 0)
        p.add(lb(RaEvent.TRACKER_SHOW, progress = "7", id = 1), 0)
        assertNull(p.view().card)
        assertTrue(p.view().trackers.isEmpty())
        p.add(lb(RaEvent.LB_SUBMITTED, progress = "7"), 0)
        assertEquals("Score 7 · sending", p.view().card!!.text)
        p.leaderboardNotes = true // switched on mid-game: the counter going on shows
        assertEquals(listOf(RaPopups.Tracker(1, "7")), p.view().trackers)
    }
}
