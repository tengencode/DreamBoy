package com.tengen.gbaemu

import com.tengen.gbaemu.DeltaSkin.Edges
import com.tengen.gbaemu.DeltaSkin.Item
import com.tengen.gbaemu.DeltaSkin.Rect
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DeltaLayoutTest {
    private val dpad = Item(KeyMap.UP or KeyMap.DOWN or KeyMap.LEFT or KeyMap.RIGHT, emptyList(), true,
        Rect(6f, 56f, 132f, 132f), Edges(15f, 15f, 15f, 15f))
    private val a = Item(KeyMap.A, emptyList(), false, Rect(241f, 62f, 64f, 64f), Edges(7f, 7f, 7f, 15f))
    private val b = Item(KeyMap.B, emptyList(), false, Rect(176f, 121f, 64f, 64f), Edges(7f, 7f, 7f, 7f))
    private val ab = Item(KeyMap.A or KeyMap.B, emptyList(), false, Rect(230f, 120f, 20f, 20f), Edges())
    private val menu = Item(0, listOf(DeltaSkin.Action.MENU), false, Rect(8f, 205f, 21f, 21f), Edges(7f, 13f, 8f, 7f))

    /** Delta's default skin upright (edgeToEdge, 320 × 274 points, no screen). */
    private val upright = DeltaSkin.Face("p.pdf", 320f, 274f, listOf(dpad, a, b, ab, menu), null, false)
    private val layout = DeltaLayout(upright, 1080f, 2424f, sideways = false)

    /** The screen point of the skin's point (x, y). */
    private fun p(x: Float, y: Float) = layout.picture.left + x * layout.scale to layout.picture.top + y * layout.scale

    private fun bitsAt(x: Float, y: Float): Int? = p(x, y).let { (px, py) -> layout.at(px, py)?.bits }

    private fun assertBox(l: Float, t: Float, r: Float, b: Float, box: TouchLayout.Box) {
        assertEquals(l, box.left, 0.01f)
        assertEquals(t, box.top, 0.01f)
        assertEquals(r, box.right, 0.01f)
        assertEquals(b, box.bottom, 0.01f)
    }

    @Test
    fun uprightThePictureFitsAtTheBottomAndTheGameIsAboveIt() {
        assertEquals(3.375f, layout.scale, 0.0001f)
        assertBox(0f, 1499.25f, 1080f, 2424f, layout.picture)
        assertBox(0f, 0f, 1080f, 1499.25f, layout.game)
        assertEquals(emptyList<Pair<Int, TouchLayout.Box>>(), layout.leftovers()) // the strip above is the game's
    }

    @Test
    fun sidewaysThePictureIsCentredAndTheGameIsTheWholeScreen() {
        val l = DeltaLayout(DeltaSkin.Face("l.pdf", 812f, 375f, emptyList(), null, true), 2424f, 1080f, sideways = true)
        assertEquals(2.88f, l.scale, 0.0001f)
        assertBox(42.72f, 0f, 2381.28f, 1080f, l.picture)
        assertBox(0f, 0f, 2424f, 1080f, l.game)
        assertEquals(emptyList<Pair<Int, TouchLayout.Box>>(), l.leftovers()) // over the game
    }

    @Test
    fun theSkinsScreenPlacesTheGameAndTheStripsAroundThePictureAreLeft() {
        val up = DeltaLayout(DeltaSkin.Face("p.png", 1290f, 2796f, emptyList(), Rect(0f, 325f, 1290f, 861f), false),
            1080f, 2424f, sideways = false)
        assertBox(0f, 83.163f, 1080f, 2424f, up.picture)
        assertBox(0f, 355.256f, 1080f, 1076.093f, up.game)
        assertEquals(1, up.leftovers().size)
        assertEquals(DeltaLayout.TOP, up.leftovers()[0].first)
        assertBox(0f, 0f, 1080f, 83.163f, up.leftovers()[0].second)
        val side = DeltaLayout(DeltaSkin.Face("l.png", 2796f, 1290f, emptyList(), Rect(615f, 60f, 1576f, 1052f), false),
            2424f, 1080f, sideways = true)
        assertEquals(listOf(DeltaLayout.LEFT, DeltaLayout.RIGHT), side.leftovers().map { it.first })
        assertBox(0f, 0f, 41.581f, 1080f, side.leftovers()[0].second)
        assertBox(2382.419f, 0f, 2424f, 1080f, side.leftovers()[1].second)
    }

    @Test
    fun aTouchAreaIsItsFrameGrownByItsExtendedEdges() {
        assertEquals(KeyMap.A, bitsAt(273f, 94f)) // A's middle
        assertEquals(KeyMap.A, bitsAt(273f, 56f)) // 6 above: inside its 7
        assertNull(bitsAt(273f, 54f)) // 8 above: nothing
        assertEquals(KeyMap.A, bitsAt(319f, 94f)) // 14 right: inside its 15
    }

    @Test
    fun whereAreasOverlapTheNearestCentreWins() {
        assertEquals(KeyMap.A or KeyMap.B, bitsAt(235f, 130f)) // inside B and the A+B button: A+B's centre is nearer
        assertEquals(KeyMap.B, bitsAt(200f, 150f))
    }

    @Test
    fun theDpadIsSplitInto9Cells() {
        assertEquals(0, bitsAt(72f, 122f)) // the middle: nothing, but it is the D-pad
        assertEquals(KeyMap.UP, bitsAt(72f, 70f))
        assertEquals(KeyMap.UP or KeyMap.RIGHT, bitsAt(130f, 60f))
        assertEquals(KeyMap.LEFT, bitsAt(10f, 122f))
        assertEquals(KeyMap.DOWN or KeyMap.LEFT, bitsAt(10f, 180f))
        assertEquals(KeyMap.UP, bitsAt(72f, 45f)) // in the extended edge above: the nearest cell
    }

    @Test
    fun actionsFireOnceWhenAFingerLands() {
        val t = DeltaTouch(layout)
        assertEquals(emptyList<DeltaSkin.Action>(), t.update(mapOf(0 to p(72f, 70f), 1 to p(273f, 94f))))
        assertEquals(KeyMap.UP or KeyMap.A, t.mask)
        assertEquals(listOf(DeltaSkin.Action.MENU), t.update(mapOf(0 to p(72f, 70f), 2 to p(18f, 215f)))) // a new finger on Menu
        assertEquals(KeyMap.UP, t.mask)
        assertEquals(emptyList<DeltaSkin.Action>(), t.update(mapOf(2 to p(18f, 215f)))) // held: nothing more
        assertEquals(emptyList<DeltaSkin.Action>(), t.update(mapOf(3 to p(200f, 150f)))) // another finger on B
        assertEquals(emptyList<DeltaSkin.Action>(), t.update(mapOf(3 to p(18f, 215f)))) // it slides onto Menu: nothing
        assertEquals(listOf(menu), t.held.toList())
        assertEquals(listOf(DeltaSkin.Action.MENU), t.update(mapOf(4 to p(18f, 215f)))) // lifted and tapped again
        t.update(emptyMap())
        assertEquals(0, t.mask)
        assertTrue(t.held.isEmpty())
    }

    @Test
    fun roundItemsAreNearlySquare() {
        assertTrue(DeltaLayout.round(Rect(0f, 0f, 64f, 64f)))
        assertTrue(DeltaLayout.round(Rect(0f, 0f, 75f, 62f)))
        assertFalse(DeltaLayout.round(Rect(0f, 0f, 97f, 29f)))
    }
}
