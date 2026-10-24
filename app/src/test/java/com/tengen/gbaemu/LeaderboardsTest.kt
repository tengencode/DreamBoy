package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class LeaderboardsTest {
    private fun parts(vararg s: String) = s.map { it.toByteArray() }.toTypedArray()

    @Test
    fun boardsAreReadFourTextsEach() {
        assertEquals(listOf(RaBoard(11, true, "Sprint", "Finish fast"), RaBoard(12, false, "Survival", "Last long")),
            RaBoard.list(parts("11", "1", "Sprint", "Finish fast", "12", "0", "Survival", "Last long")))
    }

    @Test
    fun rowsAreReadFiveTextsEach() {
        assertEquals(listOf(RaEntry(1, false, false, "Ace", "0:05.00"), RaEntry(17, true, true, "Tester", "0:12.00")),
            RaEntry.list(parts("1", "0", "0", "Ace", "0:05.00", "17", "1", "1", "Tester", "0:12.00")))
    }

    @Test
    fun theGapNamesTheRanksItSkips() {
        assertEquals("Ranks 11 to 14", gapLabel(10, 15))
        assertEquals("Rank 11", gapLabel(10, 12))
        assertEquals("Ranks 1,001 to 1,203", gapLabel(1000, 1204))
    }

    @Test
    fun aLineNeverBreaksBeforeASeparatorOrInsideANumberAndItsWord() {
        val nb = ' '
        assertEquals("Score${nb}0:10.00$nb· 1,204${nb}players$nb· best${nb}0:09.50",
            keepTogether("Score 0:10.00 · 1,204 players · best 0:09.50"))
        assertEquals("30${nb}players$nb· You haven't set a score yet", keepTogether("30 players · You haven't set a score yet"))
        assertEquals("1${nb}player", keepTogether("1 player"))
    }

    @Test
    fun scoresAreLoadingFailedOrLoaded() {
        val rows = listOf(RaEntry(1, true, false, "Tester", "1"))
        fun of(vararg status: Int, message: String = "") = BoardScores.of(status, { message }, { rows })
        assertEquals(BoardScores.Loading, of(NativeBridge.RA_BUSY, 0, 0, 0))
        assertEquals(BoardScores.Loading, of(NativeBridge.RA_NOTHING, 0, 0, 0))
        assertEquals(BoardScores.Failed("Couldn't load scores — no connection"), of(NativeBridge.RA_FAILED, BoardScores.RC_NO_RESPONSE, 0, 0))
        assertEquals(BoardScores.Failed("Couldn't load scores: Unknown leaderboard"),
            of(NativeBridge.RA_FAILED, -1, 0, 0, message = "Unknown leaderboard"))
        assertEquals(BoardScores.Failed("Couldn't load scores"), of(NativeBridge.RA_FAILED, -1, 0, 0))
        assertEquals(BoardScores.Loaded(rows, 30, true), of(NativeBridge.RA_OK, 0, 30, 1))
    }
}
