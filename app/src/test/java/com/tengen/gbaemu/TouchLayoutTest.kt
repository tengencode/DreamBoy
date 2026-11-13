package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TouchLayoutTest {
    @Test
    fun aGameBoyGameHasItsOwnPictureShapeAndNoShoulderButtons() {
        val gb = TouchLayout(1080f, 1700f, 3f, sideways = false, console = Console.GB)
        assertEquals(TouchLayout.Box(0f, 0f, 1080f, 972f), gb.picture) // full width, 10:9
        assertEquals(TouchLayout.Box(0f, 0f, 1080f, 972f), gb.game)
        assertEquals(6, gb.buttons.size) // rewind, fast-forward, A, B, Select, Start
        assertTrue(gb.buttons.none { it.bit == KeyMap.L || it.bit == KeyMap.R })
        assertEquals(listOf(LayoutItem.PICTURE, LayoutItem.DPAD, LayoutItem.REWIND, LayoutItem.FAST, LayoutItem.A, LayoutItem.B,
            LayoutItem.SELECT, LayoutItem.START), gb.items)
        // where a GBA game has its L button, a finger presses nothing
        val gba = TouchLayout(1080f, 1700f, 3f, sideways = false)
        val l = gba.box(LayoutItem.L)
        assertEquals(KeyMap.L, gba.buttonsAt(l.cx, l.cy))
        assertEquals(8, gba.buttons.size)
        assertEquals(10, gba.items.size)
        val gbcSide = TouchLayout(2400f, 1080f, 3f, sideways = true, console = Console.GBC)
        assertEquals(0, gbcSide.buttonsAt(48f * 3, 20f * 3 + 16 * 3)) // L's place sideways
        assertEquals(TouchLayout.Box(600f, 0f, 1800f, 1080f), gbcSide.game) // 10:9 by the height
        val whole = TouchLayout(2400f, 1080f, 3f, true, fitMode = Settings.SIZE_INTEGER, console = Console.GB)
        assertEquals(TouchLayout.Box(640f, 36f, 1760f, 1044f), whole.game) // 7 x 160 by 7 x 144
        // every item of a Game Boy layout can be found, moved and is kept
        val moved = gb.with(LayoutItem.A, Place(0.5f, 0.8f, 1.5f))
        assertEquals(Console.GB, moved.console)
        assertEquals(LayoutItem.A, moved.itemAt(540f, 0.8f * 1700f))
        for (item in gb.items) assertEquals(item, gb.itemAt(gb.box(item).cx, gb.box(item).cy).takeIf { item != LayoutItem.PICTURE } ?: item)
    }

    private val density = 2.625f // Pixel 9
    private val upright = TouchLayout(1080f, 1700f, density, sideways = false)
    private val sideways = TouchLayout(2400f, 1080f, density, sideways = true)
    private val delta = 0.01f

    private fun TouchLayout.button(bit: Int) = buttons.first { it.bit == bit }

    @Test
    fun dpadGivesEightDirections() {
        val d = TouchLayout.Companion
        assertEquals(KeyMap.RIGHT, d.dpadDirection(10f, 0f))
        assertEquals(KeyMap.RIGHT or KeyMap.UP, d.dpadDirection(10f, -10f))
        assertEquals(KeyMap.UP, d.dpadDirection(0f, -10f))
        assertEquals(KeyMap.UP or KeyMap.LEFT, d.dpadDirection(-10f, -10f))
        assertEquals(KeyMap.LEFT, d.dpadDirection(-10f, 0f))
        assertEquals(KeyMap.LEFT or KeyMap.DOWN, d.dpadDirection(-10f, 10f))
        assertEquals(KeyMap.DOWN, d.dpadDirection(0f, 10f))
        assertEquals(KeyMap.DOWN or KeyMap.RIGHT, d.dpadDirection(10f, 10f))
        assertEquals(KeyMap.UP, d.dpadDirection(3f, -10f)) // 17° off up: still up
    }

    @Test
    fun dpadMiddleIsDeadAndItsEdgeWorks() {
        for (l in listOf(upright, sideways)) {
            assertEquals(0, l.buttonsAt(l.dpadX, l.dpadY))
            assertEquals(0, l.buttonsAt(l.dpadX + l.dpadRadius * 0.2f, l.dpadY))
            assertEquals(KeyMap.RIGHT, l.buttonsAt(l.dpadX + l.dpadRadius * 0.9f, l.dpadY))
            assertEquals(KeyMap.UP, l.buttonsAt(l.dpadX, l.dpadY - l.dpadRadius))
        }
    }

    @Test
    fun theMiddleOfEveryButtonPressesIt() {
        for (l in listOf(upright, sideways)) {
            assertEquals(8, l.buttons.size) // 6 GBA buttons, rewind, fast-forward
            for (b in l.buttons) assertEquals("${b.label} sideways=${l.sideways}", b.bit, l.buttonsAt(b.cx, b.cy))
        }
    }

    @Test
    fun theGapBetweenAAndBPressesNothing() {
        for (l in listOf(upright, sideways)) {
            val a = l.button(KeyMap.A)
            val b = l.button(KeyMap.B)
            assertEquals("sideways=${l.sideways}", 0, l.buttonsAt((a.cx + b.cx) / 2, (a.cy + b.cy) / 2))
        }
    }

    @Test
    fun buttonsStayInsideTheScreen() {
        for (l in listOf(upright, sideways)) {
            for (b in l.buttons) {
                assertTrue("${b.label}", b.left >= 0f && b.top >= 0f && b.right <= l.width && b.bottom <= l.height)
            }
            assertTrue(l.dpadX - l.dpadRadius >= 0f && l.dpadY + l.dpadRadius <= l.height)
        }
    }

    @Test
    fun farFromEverythingPressesNothing() {
        assertEquals(0, upright.buttonsAt(upright.width / 2, upright.picture.bottom / 2)) // the middle of the game
        assertEquals(0, sideways.buttonsAt(sideways.width / 2, sideways.height / 2))
    }

    @Test
    fun withNothingEditedTheLayoutIsTodays() {
        // upright: the picture full width at the top (3:2), the buttons where 4c put them in the area below it
        assertEquals(TouchLayout.Box(0f, 0f, 1080f, 720f), upright.picture)
        val a = upright.button(KeyMap.A) // 4c: area 1080×980, A at (1080 − 16dp − 36dp, dpadY − 28dp)
        assertEquals(943.5f, a.cx, delta)
        assertEquals(720f + 427f, a.cy, delta)
        assertEquals(189f, a.right - a.left, delta) // 72 dp
        // sideways: the picture's box is the whole screen (the game is fitted inside it), the buttons as in 4c
        assertEquals(TouchLayout.Box(0f, 0f, 2400f, 1080f), sideways.picture)
        val sa = sideways.button(KeyMap.A)
        assertEquals(2400f - 42f - 78.75f, sa.cx, delta)
        assertEquals(1080f * 0.45f, sa.cy, delta)
    }

    @Test
    fun aMovedButtonIsPressedAtItsNewPlaceAndNotTheOld() {
        val old = upright.button(KeyMap.A)
        val moved = upright.with(LayoutItem.A, Place(0.25f, 0.9f))
        val a = moved.button(KeyMap.A)
        assertEquals(270f, a.cx, delta)
        assertEquals(1530f, a.cy, delta)
        assertEquals(KeyMap.A, moved.buttonsAt(a.cx, a.cy))
        assertEquals(0, moved.buttonsAt(old.cx, old.cy))
    }

    @Test
    fun aMovedDpadWorksWhereItIs() {
        val moved = sideways.with(LayoutItem.DPAD, Place(0.5f, 0.5f, 1.5f))
        assertEquals(1200f, moved.dpadX, delta)
        assertEquals(540f, moved.dpadY, delta)
        assertEquals(sideways.dpadRadius * 1.5f, moved.dpadRadius, delta)
        assertEquals(KeyMap.UP, moved.buttonsAt(1200f, 540f - moved.dpadRadius * 0.8f))
    }

    @Test
    fun movingKeepsThingsInsideTheScreenAndTheSizeWithinHalfToDouble() {
        val edge = upright.with(LayoutItem.A, Place(1.3f, -0.4f))
        val a = edge.button(KeyMap.A)
        assertEquals(1080f, a.right, delta)
        assertEquals(0f, a.top, delta)
        val big = upright.with(LayoutItem.B, Place(0.5f, 0.8f, 5f))
        assertEquals(2f, big.place(LayoutItem.B).scale, delta)
        assertEquals(378f, big.button(KeyMap.B).right - big.button(KeyMap.B).left, delta) // 2 × 72 dp, still round
        val small = upright.with(LayoutItem.START, Place(0.5f, 0.8f, 0.1f))
        val s = small.button(KeyMap.START)
        assertEquals(0.5f, small.place(LayoutItem.START).scale, delta)
        assertEquals(105f, s.right - s.left, delta) // half of 80 dp
        assertEquals(42f, s.bottom - s.top, delta) // half of 32 dp: the shape kept
    }

    @Test
    fun thePictureCanBeMovedAndShrunk() {
        val l = upright.with(LayoutItem.PICTURE, Place(0.5f, 0.25f, 0.5f))
        assertEquals(TouchLayout.Box(270f, 245f, 810f, 605f), l.picture)
        val tooBig = sideways.with(LayoutItem.PICTURE, Place(0.9f, 0.5f, 1.5f)) // bigger than the screen: no bigger
        assertEquals(TouchLayout.Box(0f, 0f, 2400f, 1080f), tooBig.picture)
    }

    @Test
    fun buttonsOverThePictureAreSeeThrough() {
        assertTrue(upright.buttons.none { upright.overPicture(it) }) // on black below the picture: solid
        assertFalse(upright.dpadOverPicture)
        assertTrue(sideways.buttons.all { sideways.overPicture(it) }) // over the whole-screen box: see-through
        val moved = upright.with(LayoutItem.A, Place(0.5f, 0.2f))
        assertTrue(moved.overPicture(moved.button(KeyMap.A)))
        assertFalse(moved.overPicture(moved.button(KeyMap.B)))
    }

    @Test
    fun theEditorFindsTheItemUnderAFinger() {
        val a = upright.button(KeyMap.A)
        assertEquals(LayoutItem.A, upright.itemAt(a.cx, a.cy))
        assertEquals(LayoutItem.DPAD, upright.itemAt(upright.dpadX, upright.dpadY))
        assertEquals(LayoutItem.PICTURE, upright.itemAt(540f, 300f))
        assertNull(upright.itemAt(540f, 1690f)) // black, below Select and Start
        assertEquals(LayoutItem.A, sideways.itemAt(sideways.button(KeyMap.A).cx, sideways.button(KeyMap.A).cy)) // buttons before the picture
    }

    @Test
    fun aPlaceIsWhereTheItemIsNow() {
        val a = upright.button(KeyMap.A)
        val p = upright.place(LayoutItem.A)
        assertEquals(a.cx / 1080f, p.x, 0.0001f)
        assertEquals(a.cy / 1700f, p.y, 0.0001f)
        assertEquals(1f, p.scale, 0.0001f)
        val again = upright.with(LayoutItem.A, p).button(KeyMap.A) // nothing moves
        assertEquals(a.cx, again.cx, delta)
        assertEquals(a.cy, again.cy, delta)
        assertEquals(a.right - a.left, again.right - again.left, delta)
    }

    @Test
    fun theGameInsideThePicturesBoxFollowsTheSizeMode() { // 8b: which buttons are over the game itself
        assertEquals(TouchLayout.Box(390f, 0f, 2010f, 1080f), sideways.game) // 2400 x 1080: the height is the limit
        assertEquals(TouchLayout.Box(480f, 60f, 1920f, 1020f), TouchLayout(2400f, 1080f, density, true, fitMode = Settings.SIZE_INTEGER).game)
        assertEquals(sideways.picture, TouchLayout(2400f, 1080f, density, true, fitMode = Settings.SIZE_STRETCH).game)
        assertEquals(upright.picture, upright.game) // upright the box is 3:2 already
    }

    @Test
    fun sidewaysOnlySelectAndStartAreOverTheGame() {
        assertFalse(sideways.dpadOverGame)
        for (b in sideways.buttons) {
            assertEquals(b.label, b.bit == KeyMap.SELECT || b.bit == KeyMap.START, sideways.overGame(b))
            assertTrue(sideways.overPicture(b)) // the box is the whole screen: Plain draws them all see-through
        }
        val moved = sideways.with(LayoutItem.A, Place(0.5f, 0.5f))
        assertTrue(moved.overGame(moved.button(KeyMap.A)))
    }
}
