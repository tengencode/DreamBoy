package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DeltaSkinTest {
    private val full = """
        {
          "name": "Test Skin", "identifier": "com.test.full", "gameTypeIdentifier": "com.rileytestut.delta.game.gba", "debug": false,
          "representations": {
            "iphone": {
              "edgeToEdge": {
                "portrait": {
                  "assets": {"resizable": "e2e_portrait.pdf"},
                  "mappingSize": {"width": 414, "height": 896},
                  "extendedEdges": {"top": 7, "bottom": 7, "left": 7, "right": 7},
                  "screens": [{"inputFrame": {"x": 0, "y": 0, "width": 240, "height": 160},
                               "outputFrame": {"x": 0, "y": 103, "width": 414, "height": 276}}],
                  "items": [
                    {"inputs": {"up": "up", "down": "down", "left": "left", "right": "right"},
                     "frame": {"x": 20, "y": 560, "width": 140, "height": 140},
                     "extendedEdges": {"top": 15, "bottom": 15, "left": 15, "right": 15}},
                    {"inputs": ["a"], "frame": {"x": 313, "y": 540, "width": 47.5, "height": 47.5}, "extendedEdges": {"right": 15}},
                    {"inputs": ["a", "b"], "frame": {"x": 300, "y": 600, "width": 20, "height": 20}},
                    {"inputs": ["l"], "frame": {"x": 0, "y": 480, "width": 97, "height": 29}},
                    {"inputs": ["select"], "frame": {"x": 150, "y": 800, "width": 20, "height": 20}},
                    {"inputs": ["start"], "frame": {"x": 200, "y": 800, "width": 20, "height": 20}},
                    {"inputs": ["menu"], "frame": {"x": 10, "y": 800, "width": 20, "height": 20}},
                    {"inputs": ["quickSave"], "frame": {"x": 40, "y": 800, "width": 20, "height": 20}},
                    {"inputs": ["quickLoad"], "frame": {"x": 70, "y": 800, "width": 20, "height": 20}},
                    {"inputs": ["fastForward"], "frame": {"x": 100, "y": 800, "width": 20, "height": 20}},
                    {"inputs": ["toggleFastForward"], "frame": {"x": 130, "y": 800, "width": 20, "height": 20}},
                    {"inputs": ["x"], "frame": {"x": 250, "y": 800, "width": 20, "height": 20}},
                    {"inputs": {"up": "analogStickUp", "down": "analogStickDown", "left": "analogStickLeft", "right": "analogStickRight"},
                     "frame": {"x": 300, "y": 800, "width": 80, "height": 80}, "thumbstick": {"name": "s.pdf", "width": 85, "height": 87}}
                  ]
                }
              },
              "standard": {
                "portrait": {"assets": {"resizable": "std_portrait.pdf"}, "mappingSize": {"width": 320, "height": 240}, "items": []},
                "landscape": {
                  "assets": {"small": "land_small.png", "medium": "land_medium.png", "large": "land_large.png"},
                  "mappingSize": {"width": 667, "height": 375},
                  "gameScreenFrame": {"x": 100, "y": 10, "width": 467, "height": 311},
                  "translucent": true,
                  "items": [{"inputs": ["b"], "frame": {"x": 500, "y": 280, "width": 75, "height": 75}}]
                }
              }
            },
            "ipad": {"standard": {"portrait": {"assets": {"resizable": "ipad.pdf"}, "mappingSize": {"width": 768, "height": 1024}, "items": []}}}
          }
        }
    """.trimIndent()

    private val portrait = """"iphone": {"standard": {"portrait": {"assets": {"small": "p.png"}, "mappingSize": {"width": 320, "height": 240},
        "items": [{"inputs": ["a"], "frame": {"x": 1, "y": 2, "width": 3, "height": 4}}]}}}"""

    private fun minimal(type: String = DeltaSkin.GBA, phone: String = portrait) =
        """{"name": "M", "identifier": "com.test.m", "gameTypeIdentifier": "$type", "representations": {$phone}}"""

    private fun problem(json: String): SkinProblem? = try {
        DeltaSkin.parse(json)
        null
    } catch (e: SkinException) {
        e.problem
    }

    @Test
    fun readsTheNameAndEachDirectionsPhoneLayout() {
        val s = DeltaSkin.parse(full)
        assertEquals("Test Skin", s.name)
        assertEquals("com.test.full", s.identifier)
        val up = s.upright!!
        assertEquals("e2e_portrait.pdf", up.image) // edgeToEdge before standard
        assertEquals(414f, up.width)
        assertEquals(896f, up.height)
        assertEquals(false, up.translucent)
        val side = s.sideways!! // edgeToEdge has no landscape: standard's
        assertEquals("land_large.png", side.image) // the biggest PNG
        assertEquals(667f, side.width)
        assertEquals(true, side.translucent)
        assertEquals(up, s.face(false))
        assertEquals(side, s.face(true))
    }

    @Test
    fun inputsBecomeOurButtonsAndActionsOthersAreDropped() {
        val items = DeltaSkin.parse(full).upright!!.items
        val dpad = KeyMap.UP or KeyMap.DOWN or KeyMap.LEFT or KeyMap.RIGHT
        assertEquals(listOf(dpad, KeyMap.A, KeyMap.A or KeyMap.B, KeyMap.L, KeyMap.SELECT, KeyMap.START, 0, 0, 0, KeyMap.FAST, KeyMap.FAST),
            items.map { it.bits }) // "x" and the analog stick are gone
        assertEquals(listOf(true) + List(10) { false }, items.map { it.dpad })
        assertEquals(listOf(DeltaSkin.Action.MENU), items[6].actions)
        assertEquals(listOf(DeltaSkin.Action.QUICK_SAVE), items[7].actions)
        assertEquals(listOf(DeltaSkin.Action.QUICK_LOAD), items[8].actions)
        assertEquals(emptyList<DeltaSkin.Action>(), items[0].actions)
    }

    @Test
    fun framesExtendedEdgesAndTheGameScreen() {
        val s = DeltaSkin.parse(full)
        val items = s.upright!!.items
        assertEquals(DeltaSkin.Rect(313f, 540f, 47.5f, 47.5f), items[1].frame)
        assertEquals(DeltaSkin.Edges(15f, 15f, 15f, 15f), items[0].extended) // the item's own
        assertEquals(DeltaSkin.Edges(7f, 7f, 7f, 15f), items[1].extended) // right its own, the rest the layout's
        assertEquals(DeltaSkin.Edges(7f, 7f, 7f, 7f), items[3].extended) // the layout's
        assertEquals(DeltaSkin.Edges(), s.sideways!!.items[0].extended) // none anywhere: 0
        assertEquals(DeltaSkin.Rect(0f, 103f, 414f, 276f), s.upright!!.screen) // screens[0].outputFrame
        assertEquals(DeltaSkin.Rect(100f, 10f, 467f, 311f), s.sideways!!.screen) // gameScreenFrame
    }

    @Test
    fun noScreenAByteOrderMarkAndTheConsoleNameInAnyCase() {
        val s = DeltaSkin.parse("﻿" + minimal(type = "com.rileytestut.Delta.game.gba"))
        assertNull(s.upright!!.screen)
        assertEquals("p.png", s.upright!!.image)
        assertNull(s.sideways)
    }

    @Test
    fun problems() {
        assertEquals(SkinProblem.OTHER_CONSOLE, problem(minimal(type = "com.rileytestut.delta.game.nes")))
        assertEquals("This skin is for another console. Only Game Boy, Game Boy Color and GBA skins work.",
            SkinProblem.OTHER_CONSOLE.message)
        assertEquals(SkinProblem.NO_PHONE_LAYOUT, problem(minimal(phone = """"ipad": {"standard": {}}""")))
        assertEquals(SkinProblem.NOT_A_SKIN, problem("{"))
        assertEquals(SkinProblem.NOT_A_SKIN, problem("[]"))
        assertEquals(SkinProblem.NOT_A_SKIN, problem("""{"name": "M", "representations": {$portrait}}""")) // no identifier, no console
        assertEquals(SkinProblem.NOT_A_SKIN, problem(minimal(phone = portrait.replace("\"mappingSize\"", "\"size\""))))
        assertEquals(SkinProblem.NOT_A_SKIN, problem(minimal(phone = portrait.replace("\"frame\"", "\"place\""))))
        assertEquals(SkinProblem.NOT_A_SKIN, problem(minimal(phone = portrait.replace("\"small\"", "\"tiny\"")))) // no picture
        // a size that can't be drawn: nothing to scale by, or a picture far too thin
        for (size in listOf("\"width\": 0, \"height\": 0", "\"width\": 320, \"height\": -240", "\"width\": 1, \"height\": 100000")) {
            val phone = portrait.replace("\"width\": 320, \"height\": 240", size)
            assertTrue(size, phone != portrait)
            assertEquals(size, SkinProblem.NOT_A_SKIN, problem(minimal(phone = phone)))
        }
        assertEquals(SkinProblem.NOT_A_SKIN, problem("[".repeat(200_000))) // nested too deep: no crash
    }

    @Test
    fun aGameBoyColorSkinIsTheGameBoyFamilys() {
        assertEquals("gb", DeltaSkin.parse(minimal(DeltaSkin.GBC)).family)
        assertEquals("gb", DeltaSkin.parse(minimal("COM.RILEYTESTUT.DELTA.GAME.GBC")).family)
    }

    @Test
    fun aGbaSkinIsTheGbaFamilys() {
        assertEquals("gba", DeltaSkin.parse(minimal()).family)
    }

    @Test
    fun shouldersPressNothingInAGameBoySkin() {
        fun item(inputs: String) = """{"inputs": $inputs, "frame": {"x": 1, "y": 2, "width": 3, "height": 4}}"""
        val phone = portrait.replace(item("""["a"]"""),
            item("""["l"]""") + "," + item("""["a", "l"]""") + "," + item("""["r", "menu"]"""))
        assertTrue(phone != portrait)
        val gb = DeltaSkin.parse(minimal(DeltaSkin.GBC, phone)).upright!!.items
        assertEquals(listOf(KeyMap.A, 0), gb.map { it.bits }) // the item with only L is gone
        assertEquals(listOf(DeltaSkin.Action.MENU), gb[1].actions)
        val gba = DeltaSkin.parse(minimal(DeltaSkin.GBA, phone)).upright!!.items
        assertEquals(listOf(KeyMap.L, KeyMap.A or KeyMap.L, KeyMap.R), gba.map { it.bits })
    }
}
