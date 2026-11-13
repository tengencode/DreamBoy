package com.tengen.gbaemu

import java.text.DateFormat
import java.util.Calendar
import java.util.Date
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** When a game was last played, as the game list shows it (12: the grid's one-line form). */
class PlayedTextTest {
    @Test
    fun theGripLineStartsWithTheConsolesMark() {
        assertEquals("GBC · Today", gripLine(Console.GBC, "Today"))
        assertEquals("GB · Never played · outside the folder", gripLine(Console.GB, "Never played", outside = true))
        assertEquals("GBA · 12 Nov 2026", gripLine(Console.GBA, "12 Nov 2026"))
        assertEquals("Yesterday", gripLine(null, "Yesterday")) // not known yet: no mark
        assertEquals("Today · outside the folder", gripLine(null, "Today", outside = true))
    }

    private val now = Calendar.getInstance().apply { set(2026, Calendar.OCTOBER, 5, 14, 0) }.timeInMillis
    private fun daysAgo(n: Int) = Calendar.getInstance().apply { timeInMillis = now; add(Calendar.DAY_OF_YEAR, -n); set(Calendar.HOUR_OF_DAY, 9) }.timeInMillis

    @Test
    fun theGridSaysOnlyTheDay() {
        assertEquals("Never played", playedDay(0, now))
        assertEquals("Today", playedDay(daysAgo(0), now))
        assertEquals("Yesterday", playedDay(daysAgo(1), now))
        assertEquals(DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(daysAgo(7))), playedDay(daysAgo(7), now))
    }

    @Test
    fun theContinueCardKeepsTheTime() {
        assertTrue(playedText(daysAgo(0), now).startsWith("Today "))
        assertTrue(playedText(daysAgo(1), now).startsWith("Yesterday "))
    }
}
