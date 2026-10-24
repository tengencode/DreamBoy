package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class ListCutTest {
    private val rows = listOf(0..100, 100..200, 200..300, 300..400) // four rows of 100 px

    @Test
    fun aListThatFitsKeepsItsRoom() {
        assertEquals(500f, listCut(rows, 500f, 24f))
        assertEquals(400f, listCut(rows, 400f, 24f))
        assertEquals(300f, listCut(emptyList(), 300f, 24f))
    }

    @Test
    fun aCutWellInsideARowStays() {
        assertEquals(250f, listCut(rows, 250f, 24f))
    }

    @Test
    fun aCutNearARowsEdgeMovesIntoARow() {
        assertEquals(255f, listCut(rows, 290f, 24f)) // near the bottom of row 3: just past its middle
        assertEquals(155f, listCut(rows, 205f, 24f)) // just into row 3: the row above, just past its middle
        assertEquals(155f, listCut(rows, 200f, 24f)) // exactly on a divider
    }

    @Test
    fun aListScrollsJustEnoughToShowTheUsersRow() {
        // a 300 px view; the row at 500..600 is below it: scroll so it shows with 40 px of the next row under it
        assertEquals(340, scrollToShow(500..600, view = 300, now = 0, margin = 40))
        assertEquals(0, scrollToShow(100..200, view = 300, now = 0, margin = 40)) // already in view
        assertEquals(100, scrollToShow(100..200, view = 300, now = 250, margin = 40)) // above the view: its top
    }
}
