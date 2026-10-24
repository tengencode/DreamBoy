# Milestone 9 — Delta Skins — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** the user imports skins made for the Delta emulator (`.deltaskin`): one picture of the whole controller with touch areas; the game is drawn where the skin says; Delta's menu, fast-forward and quick save / load buttons work.

**Architecture:** Pure Kotlin reads a skin (`DeltaSkin`, through `Json.value()`), places it on the screen and turns fingers into buttons and actions (`DeltaLayout`, `DeltaTouch`), and imports / lists / removes skins in `files/skins/<n>/` (`SkinFiles`) — all tested on the PC. Android code draws the pictures (`SkinPicture`: `PdfRenderer` / `BitmapFactory`, PDF drawings kept) and the skin over the game (`DeltaSkinView`); the game's surface goes in the skin's screen box through the existing picture-box path in `GameScreen`. The emulator core and the native code are not touched.

**Tech Stack:** Kotlin, Jetpack Compose (Material 3 1.4.0), Android `PdfRenderer` / `BitmapFactory`, `java.util.zip`, JUnit 4, Compose UI test 1.11.4, Gradle `--offline`, Git Bash `tools/dev.sh`.

**Spec:** `docs/superpowers/specs/2026-10-04-milestone-9-delta-skins-design.md`

---

## Conventions

- Git Bash from the repo root, on the branch `milestone-9`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push>` (the Pixel 9 phone emulator `Pixel_9_API_36_1`; never the user's USB phones unless the user asks). Gradle runs `--offline`; nothing new is downloaded. A missing artifact → stop and ask the user. Ask before restarting or cold-booting the emulator.
- The core and the native code are not touched: CTest stays 120/120 (run once in Task 9).
- Counting tests: Kotlin unit tests `ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'` (total, failures); on-device `grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml` after `rm -rf app/build/outputs/androidTest-results`.
- Kotlin: 96 → 99 (Task 1) → 104 (Task 2) → 112 (Task 3) → 118 (Task 4) → 119 (Task 5). On-device: 58 → 61 (Task 6) → 62 (Task 7) → 63 (Task 8).
- Test-first: each task's tests are written first and fail (they don't compile without the new code), then the code. The drawing itself is checked by Claude on the emulator in Task 9.
- One commit per task; messages end with the Co-Authored-By trailer. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes). The tasks run one after another without stopping; Task 9 ends with **the user's try** (a stop) and the merge question. After every task: all documents updated and checked for anything stale.
- The test skins are in `tests/skins/` (gitignored, never committed): `Standard.deltaskin` (Delta's default GBA skin) and `r3bel/` (R3BEL Neumorphius: `info.json` + 2 PNGs). The prototype (branch `proto-9`, `50d1957`) is only a reference; this plan's code replaces it.

## File map

```
app/src/main/java/com/tengen/gbaemu/Json.kt; app/src/test/java/com/tengen/gbaemu/JsonTest.kt (new)                                   Task 1
app/src/main/java/com/tengen/gbaemu/DeltaSkin.kt (new); app/src/test/java/com/tengen/gbaemu/DeltaSkinTest.kt (new)                   Task 2
app/src/main/java/com/tengen/gbaemu/DeltaLayout.kt (new); app/src/test/java/com/tengen/gbaemu/DeltaLayoutTest.kt (new)               Task 3
app/src/main/java/com/tengen/gbaemu/SkinFiles.kt (new); app/src/test/java/com/tengen/gbaemu/SkinFilesTest.kt (new)                   Task 4
app/src/main/java/com/tengen/gbaemu/Settings.kt, States.kt; app/src/test/java/com/tengen/gbaemu/SettingsTest.kt, StatesTest.kt        Task 5
app/src/main/java/com/tengen/gbaemu/SkinPicture.kt (new), DeltaSkinView.kt (new);
  app/src/androidTest/java/com/tengen/gbaemu/SkinPictureTest.kt (new)                                                               Task 6
app/src/main/java/com/tengen/gbaemu/AppTheme.kt, SettingsScreens.kt; app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt       Task 7
app/src/main/java/com/tengen/gbaemu/Screens.kt, MainActivity.kt; app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt           Task 8
(Claude's check on the emulator, the user's try, docs, tag)                                                                         Task 9
```

---

### Task 1: Decimals and whole values in `Json`

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Json.kt` (after `number()`)
- Test: `app/src/test/java/com/tengen/gbaemu/JsonTest.kt` (new, 3 tests)

- [x] **Step 1: Write the failing test**

Create `app/src/test/java/com/tengen/gbaemu/JsonTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class JsonTest {
    @Test
    fun decimalsReadWithFractionsAndExponents() {
        assertEquals(47.5, Json.Reader("47.5").decimal(), 0.0)
        assertEquals(-3.0, Json.Reader(" -3").decimal(), 0.0)
        assertEquals(100.0, Json.Reader("1e2").decimal(), 0.0)
        assertEquals(0.25, Json.Reader("2.5E-1").decimal(), 0.0)
    }

    @Test
    fun aValueIsReadAsPlainKotlin() {
        val v = Json.Reader("""{"a": [1, 2.5, "x\"y", true, false, null], "b": {"c": {}}, "d": []}""").run { value().also { end() } }
        assertEquals(mapOf("a" to listOf(1.0, 2.5, "x\"y", true, false, null), "b" to mapOf("c" to emptyMap<String, Any?>()),
            "d" to emptyList<Any?>()), v)
        assertEquals(listOf("a", "b", "d"), (v as Map<*, *>).keys.toList()) // in the file's order
    }

    @Test
    fun brokenJsonThrows() {
        for (bad in listOf("{", "[1,", "{\"a\" 1}", "-", "nul", "\"open")) {
            assertThrows(bad, IllegalStateException::class.java) { Json.Reader(bad).value() }
        }
    }
}
```

- [x] **Step 2: Run it to make sure it fails**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -5`
Expected: FAIL — `Unresolved reference 'decimal'` / `'value'`.

- [x] **Step 3: Write the code**

In `app/src/main/java/com/tengen/gbaemu/Json.kt`, change the object's comment's first line to `The small JSON the app writes (cheats, the game list) and reads (also Delta skins, 9): strings, numbers, true/false,` (rest unchanged) and add after `number()`:

```kotlin
        /** A number with an optional fraction and exponent (`47.5`, `-3`, `1e2`). */
        fun decimal(): Double {
            peek()
            val start = i
            while (i < s.length && (s[i].isDigit() || s[i] in "+-.eE")) i++
            return s.substring(start, i).toDoubleOrNull() ?: error("expected a number at $start")
        }

        /** Any value as plain Kotlin: an object a map (keys in order), a list, a string, a number (Double), true/false, null. */
        fun value(): Any? = when (peek()) {
            '{' -> LinkedHashMap<String, Any?>().also { m -> obj { k -> m[k] = value() } }
            '[' -> ArrayList<Any?>().also { l -> list { l += value() } }
            '"' -> string()
            't', 'f' -> bool()
            'n' -> if (nullValue()) null else error("expected null at $i")
            else -> decimal()
        }
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3` then the count command.
Expected: BUILD SUCCESSFUL; `99 0`.

- [x] **Step 5: Commit** (with `docs/PROGRESS.md` and this plan's ticks)

```bash
git add app/src/main/java/com/tengen/gbaemu/Json.kt app/src/test/java/com/tengen/gbaemu/JsonTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-9-delta-skins.md
git commit -m "feat(app): Json reads decimals and whole values (for Delta skins)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Reading a Delta skin

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/DeltaSkin.kt` (`SkinProblem`, `SkinException`, `DeltaSkin` with `Face`, `Item`, `Rect`, `Edges`, `Action`, `parse`)
- Test: `app/src/test/java/com/tengen/gbaemu/DeltaSkinTest.kt` (new, 5 tests)

- [x] **Step 1: Write the failing test**

Create `app/src/test/java/com/tengen/gbaemu/DeltaSkinTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
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
        assertEquals(SkinProblem.OTHER_CONSOLE, problem(minimal(type = "com.rileytestut.delta.game.gbc")))
        assertEquals(SkinProblem.NO_PHONE_LAYOUT, problem(minimal(phone = """"ipad": {"standard": {}}""")))
        assertEquals(SkinProblem.NOT_A_SKIN, problem("{"))
        assertEquals(SkinProblem.NOT_A_SKIN, problem("[]"))
        assertEquals(SkinProblem.NOT_A_SKIN, problem("""{"name": "M", "representations": {$portrait}}""")) // no identifier, no console
        assertEquals(SkinProblem.NOT_A_SKIN, problem(minimal(phone = portrait.replace("\"mappingSize\"", "\"size\""))))
        assertEquals(SkinProblem.NOT_A_SKIN, problem(minimal(phone = portrait.replace("\"frame\"", "\"place\""))))
        assertEquals(SkinProblem.NOT_A_SKIN, problem(minimal(phone = portrait.replace("\"small\"", "\"tiny\"")))) // no picture
    }
}
```

- [x] **Step 2: Run it to make sure it fails**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -5`
Expected: FAIL — `Unresolved reference 'DeltaSkin'`.

- [x] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/DeltaSkin.kt`:

```kotlin
package com.tengen.gbaemu

/** Why a skin can't be imported or used (9): the message the user sees. */
enum class SkinProblem(val message: String) {
    NOT_A_SKIN("This file isn't a skin."),
    OTHER_CONSOLE("This skin is for another console. Only GBA skins work."),
    NO_PHONE_LAYOUT("This skin has no phone layout."),
    PICTURE_MISSING("This skin's picture is missing."),
    TOO_BIG("This skin is too big."),
}

class SkinException(val problem: SkinProblem) : Exception(problem.message)

/**
 * A skin made for the Delta emulator (9), read from its `info.json`: the phone layout for each direction — upright
 * (Delta's portrait) and sideways (landscape), each from `iphone.edgeToEdge` (the tall iPhones, closest to Android
 * phones), else `iphone.standard`; either may be missing. Coordinates are Delta's points in the picture's
 * `mappingSize`. Pure Kotlin, so it is tested on the PC.
 */
class DeltaSkin(val name: String, val identifier: String, val upright: Face?, val sideways: Face?) {
    enum class Action { MENU, QUICK_SAVE, QUICK_LOAD }

    data class Rect(val x: Float, val y: Float, val width: Float, val height: Float)
    data class Edges(val top: Float = 0f, val bottom: Float = 0f, val left: Float = 0f, val right: Float = 0f)

    /** A touch area: the buttons it presses (KeyMap bits, FAST too), the actions it fires; a [dpad] is split 3 × 3. */
    data class Item(val bits: Int, val actions: List<Action>, val dpad: Boolean, val frame: Rect, val extended: Edges)

    /**
     * One layout: its picture's file name (PDF or PNG), the picture's size, the touch areas, where the game goes (null:
     * the skin doesn't say) and whether the picture is see-through.
     */
    data class Face(val image: String, val width: Float, val height: Float, val items: List<Item>, val screen: Rect?,
                    val translucent: Boolean)

    fun face(sideways: Boolean): Face? = if (sideways) this.sideways else upright

    companion object {
        const val GBA = "com.rileytestut.delta.game.gba"

        /** Reads `info.json`; throws [SkinException] (NOT_A_SKIN, OTHER_CONSOLE, NO_PHONE_LAYOUT). */
        fun parse(json: String): DeltaSkin {
            val root = try {
                Json.Reader(json.removePrefix("﻿")).run { value().also { end() } }
            } catch (e: IllegalStateException) {
                null
            }
            val m = map(root)
            val name = m["name"] as? String ?: fail(SkinProblem.NOT_A_SKIN)
            val id = m["identifier"] as? String ?: fail(SkinProblem.NOT_A_SKIN)
            val type = m["gameTypeIdentifier"] as? String ?: fail(SkinProblem.NOT_A_SKIN)
            if (!type.equals(GBA, ignoreCase = true)) fail(SkinProblem.OTHER_CONSOLE)
            val iphone = (m["representations"] as? Map<*, *>)?.get("iphone") as? Map<*, *>
            fun layout(direction: String): Face? = listOf("edgeToEdge", "standard").firstNotNullOfOrNull {
                ((iphone?.get(it) as? Map<*, *>)?.get(direction) as? Map<*, *>)
            }?.let(::face)
            val upright = layout("portrait")
            val sideways = layout("landscape")
            if (upright == null && sideways == null) fail(SkinProblem.NO_PHONE_LAYOUT)
            return DeltaSkin(name, id, upright, sideways)
        }

        private val INPUTS = mapOf(
            "a" to KeyMap.A, "b" to KeyMap.B, "l" to KeyMap.L, "r" to KeyMap.R, "select" to KeyMap.SELECT, "start" to KeyMap.START,
            "up" to KeyMap.UP, "down" to KeyMap.DOWN, "left" to KeyMap.LEFT, "right" to KeyMap.RIGHT,
            "fastForward" to KeyMap.FAST, "toggleFastForward" to KeyMap.FAST, // our ⏩ is tap or hold
        )
        private val ACTIONS = mapOf("menu" to Action.MENU, "quickSave" to Action.QUICK_SAVE, "quickLoad" to Action.QUICK_LOAD)

        private fun fail(p: SkinProblem): Nothing = throw SkinException(p)

        private fun map(v: Any?): Map<*, *> = v as? Map<*, *> ?: fail(SkinProblem.NOT_A_SKIN)

        private fun num(m: Map<*, *>, key: String): Float = (m[key] as? Double)?.toFloat() ?: fail(SkinProblem.NOT_A_SKIN)

        private fun rect(v: Any?): Rect = map(v).let { Rect(num(it, "x"), num(it, "y"), num(it, "width"), num(it, "height")) }

        /** [v]'s sides; a side it doesn't give is [d]'s. */
        private fun edges(v: Any?, d: Edges): Edges {
            val m = v as? Map<*, *> ?: return d
            fun side(key: String, default: Float) = (m[key] as? Double)?.toFloat() ?: default
            return Edges(side("top", d.top), side("bottom", d.bottom), side("left", d.left), side("right", d.right))
        }

        private fun face(m: Map<*, *>): Face {
            val assets = map(m["assets"])
            val image = listOf("resizable", "large", "medium", "small").firstNotNullOfOrNull { assets[it] as? String }
                ?: fail(SkinProblem.NOT_A_SKIN)
            val size = map(m["mappingSize"])
            val extended = edges(m["extendedEdges"], Edges())
            val items = (m["items"] as? List<*> ?: emptyList<Any?>()).mapNotNull { item(map(it), extended) }
            val screen = ((m["screens"] as? List<*>)?.firstOrNull() as? Map<*, *>)?.get("outputFrame")?.let(::rect)
                ?: m["gameScreenFrame"]?.let(::rect)
            return Face(image, num(size, "width"), num(size, "height"), items, screen, m["translucent"] == true)
        }

        /** An item, or null when none of its inputs is one we use (X, Y, the analog stick, the touch screen, ...). */
        private fun item(m: Map<*, *>, extended: Edges): Item? {
            val inputs = m["inputs"]
            val names = when (inputs) {
                is Map<*, *> -> inputs.values.mapNotNull { it as? String } // a D-pad: {"up": "up", ...}
                is List<*> -> inputs.mapNotNull { it as? String }
                else -> emptyList()
            }
            var bits = 0
            val actions = ArrayList<Action>()
            for (n in names) {
                INPUTS[n]?.let { bits = bits or it }
                ACTIONS[n]?.let { actions += it }
            }
            if (bits == 0 && actions.isEmpty()) return null
            return Item(bits, actions, inputs is Map<*, *>, rect(m["frame"]), edges(m["extendedEdges"], extended))
        }
    }
}
```

(The comment on `item` says "..." inside a comment — `UiTextTest` skips comments.)

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3` then the count command.
Expected: BUILD SUCCESSFUL; `104 0`.

- [x] **Step 5: Commit** (with `docs/PROGRESS.md` and this plan's ticks)

```bash
git add app/src/main/java/com/tengen/gbaemu/DeltaSkin.kt app/src/test/java/com/tengen/gbaemu/DeltaSkinTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-9-delta-skins.md
git commit -m "feat(app): reading a Delta skin's info.json

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Placing a skin and the fingers on it

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/DeltaLayout.kt` (`DeltaLayout`: `scale`, `picture`, `game`, `box`, `at`, `leftovers`, `round`; `DeltaTouch`: `update`, `mask`, `held`)
- Test: `app/src/test/java/com/tengen/gbaemu/DeltaLayoutTest.kt` (new, 8 tests)

- [x] **Step 1: Write the failing test**

Create `app/src/test/java/com/tengen/gbaemu/DeltaLayoutTest.kt` (numbers from the real skins on the Pixel 9, 1080 × 2424, as measured in the prototype):

```kotlin
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
```

- [x] **Step 2: Run it to make sure it fails**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -5`
Expected: FAIL — `Unresolved reference 'DeltaLayout'`.

- [x] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/DeltaLayout.kt`:

```kotlin
package com.tengen.gbaemu

import kotlin.math.hypot
import kotlin.math.min

/**
 * A Delta skin's layout [face] on a [width] × [height] screen, in pixels (9): the picture scaled to fit whole —
 * upright at the bottom, centred across; sideways centred — the game's box (the skin's screen; without one, upright the
 * area above the picture, sideways the whole screen), what a finger presses, and the strips the picture leaves at the
 * screen's edges. Pure maths, so it is tested on the PC.
 */
class DeltaLayout(val face: DeltaSkin.Face, val width: Float, val height: Float, val sideways: Boolean) {
    /** What a finger is on: the item and, for a D-pad, the directions of its cell (else the item's buttons). */
    data class Hit(val item: DeltaSkin.Item, val bits: Int)

    val scale = min(width / face.width, height / face.height)
    val picture: TouchLayout.Box
    val game: TouchLayout.Box

    init {
        val w = face.width * scale
        val h = face.height * scale
        val left = (width - w) / 2
        val top = if (sideways) (height - h) / 2 else height - h
        picture = TouchLayout.Box(left, top, left + w, top + h)
        game = face.screen?.let(::box)
            ?: if (sideways) TouchLayout.Box(0f, 0f, width, height) else TouchLayout.Box(0f, 0f, width, top)
    }

    /** A skin rectangle on the screen. */
    fun box(r: DeltaSkin.Rect) = TouchLayout.Box(picture.left + r.x * scale, picture.top + r.y * scale,
        picture.left + (r.x + r.width) * scale, picture.top + (r.y + r.height) * scale)

    /** The item a finger at (x, y) is on: of those whose area (frame + extended edges) holds it, the nearest centre. */
    fun at(x: Float, y: Float): Hit? {
        var best: DeltaSkin.Item? = null
        var bestDistance = Float.MAX_VALUE
        for (item in face.items) {
            val b = box(item.frame)
            val e = item.extended
            if (x < b.left - e.left * scale || x > b.right + e.right * scale ||
                y < b.top - e.top * scale || y > b.bottom + e.bottom * scale) continue
            val d = hypot(x - b.cx, y - b.cy)
            if (d < bestDistance) {
                best = item
                bestDistance = d
            }
        }
        val item = best ?: return null
        if (!item.dpad) return Hit(item, item.bits)
        val b = box(item.frame) // 3 × 3: the middle nothing, the sides one direction, the corners two
        val column = ((x - b.left) / (b.right - b.left) * 3).toInt().coerceIn(0, 2)
        val row = ((y - b.top) / (b.bottom - b.top) * 3).toInt().coerceIn(0, 2)
        var bits = 0
        if (row == 0) bits = bits or KeyMap.UP
        if (row == 2) bits = bits or KeyMap.DOWN
        if (column == 0) bits = bits or KeyMap.LEFT
        if (column == 2) bits = bits or KeyMap.RIGHT
        return Hit(item, bits and item.bits)
    }

    /** The strips between the picture and the screen's edges ([TOP], [BOTTOM], [LEFT], [RIGHT]), except over the game. */
    fun leftovers(): List<Pair<Int, TouchLayout.Box>> = listOf(
        TOP to TouchLayout.Box(0f, 0f, width, picture.top),
        BOTTOM to TouchLayout.Box(0f, picture.bottom, width, height),
        LEFT to TouchLayout.Box(0f, picture.top, picture.left, picture.bottom),
        RIGHT to TouchLayout.Box(picture.right, picture.top, width, picture.bottom),
    ).filter { (_, b) -> b.right - b.left >= 0.5f && b.bottom - b.top >= 0.5f && !b.overlaps(game) }

    companion object {
        const val TOP = 0
        const val BOTTOM = 1
        const val LEFT = 2
        const val RIGHT = 3

        /** A nearly square item is a round button: darkened as the oval inside its frame. */
        fun round(r: DeltaSkin.Rect) = r.width / r.height in 0.8f..1.25f
    }
}

/**
 * The fingers on a Delta skin (9): the buttons held ([mask]), the items under a finger ([held], darkened), and the
 * actions to fire — once, when a new finger lands on the item (not when one slides onto it, nor while it is held).
 */
class DeltaTouch(private val layout: DeltaLayout) {
    private var on = HashMap<Int, DeltaSkin.Item>()
    private var fingers = emptySet<Int>()

    var mask = 0
        private set

    val held: Collection<DeltaSkin.Item> get() = on.values

    /** [now]: pointer id → (x, y) of every finger down. Returns the actions to fire. */
    fun update(now: Map<Int, Pair<Float, Float>>): List<DeltaSkin.Action> {
        val items = HashMap<Int, DeltaSkin.Item>()
        var bits = 0
        for ((id, point) in now) {
            val hit = layout.at(point.first, point.second) ?: continue
            bits = bits or hit.bits
            items[id] = hit.item
        }
        val fire = items.filterKeys { it !in fingers }.values.flatMap { it.actions }
        on = items
        fingers = now.keys.toSet()
        mask = bits
        return fire
    }
}
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3` then the count command.
Expected: BUILD SUCCESSFUL; `112 0`.

- [x] **Step 5: Commit** (with `docs/PROGRESS.md` and this plan's ticks)

```bash
git add app/src/main/java/com/tengen/gbaemu/DeltaLayout.kt app/src/test/java/com/tengen/gbaemu/DeltaLayoutTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-9-delta-skins.md
git commit -m "feat(app): placing a Delta skin on the screen and the fingers on it

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Importing, listing and removing skins

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/SkinFiles.kt` (`SkinFiles`: `root`, `list`, `get`, `import`, `remove`; `Imported`; `MAX_FILES`, `MAX_BYTES`, `safe`)
- Test: `app/src/test/java/com/tengen/gbaemu/SkinFilesTest.kt` (new, 6 tests)

- [x] **Step 1: Write the failing test**

Create `app/src/test/java/com/tengen/gbaemu/SkinFilesTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class SkinFilesTest {
    @get:Rule
    val tmp = TemporaryFolder()

    private val thumbs = ArrayList<String>()

    private fun info(id: String = "com.test.one", name: String = "One", type: String = DeltaSkin.GBA, image: String = "p.png") =
        """{"name": "$name", "identifier": "$id", "gameTypeIdentifier": "$type", "representations": {"iphone": {"edgeToEdge":
        {"portrait": {"assets": {"small": "$image"}, "mappingSize": {"width": 320, "height": 240},
        "items": [{"inputs": ["a"], "frame": {"x": 1, "y": 2, "width": 3, "height": 4}}]}}}}}"""

    private fun zip(vararg entries: Pair<String, String>): InputStream {
        val out = ByteArrayOutputStream()
        ZipOutputStream(out).use { z ->
            for ((name, text) in entries) {
                z.putNextEntry(ZipEntry(name))
                z.write(text.toByteArray())
                z.closeEntry()
            }
        }
        return ByteArrayInputStream(out.toByteArray())
    }

    /** The tile picture is made by Android's drawing in the app; here a stand-in writes a text. */
    private fun files(maxBytes: Long = SkinFiles.MAX_BYTES) = SkinFiles(tmp.root, { dir, face, out ->
        thumbs += face.image
        out.writeText("tile of " + File(dir, face.image).readText())
    }, maxBytes)

    private fun problem(f: SkinFiles, input: InputStream): SkinProblem? = try {
        f.import(input)
        null
    } catch (e: SkinException) {
        e.problem
    }

    @Test
    fun aGoodSkinIsStoredAsNumberOneWithItsTilePicture() {
        val f = files()
        val s = f.import(zip("info.json" to info(), "p.png" to "picture"))
        assertEquals(1, s.number)
        assertEquals(File(tmp.root, "skins/1"), s.dir)
        assertEquals("picture", File(s.dir, "p.png").readText())
        assertEquals("tile of picture", s.thumb.readText())
        assertEquals(listOf("p.png"), thumbs)
        assertEquals(listOf("One"), f.list().map { it.skin.name })
        assertEquals("One", f.get(1)!!.skin.name)
        assertFalse(File(tmp.root, "skins/.incoming").exists())
    }

    @Test
    fun infoJsonInsideOneTopFolderWithAMacFolderBeside() {
        val s = files().import(zip("My Skin/info.json" to info(), "My Skin/p.png" to "picture", "__MACOSX/My Skin/._p.png" to "x"))
        assertTrue(File(s.dir, "info.json").isFile)
        assertTrue(File(s.dir, "p.png").isFile)
    }

    @Test
    fun theSameSkinAgainReplacesItsCopyAnotherGetsTheNextNumber() {
        val f = files()
        assertEquals(1, f.import(zip("info.json" to info(), "p.png" to "a")).number)
        assertEquals(2, f.import(zip("info.json" to info(id = "com.test.two", name = "Two"), "p.png" to "b")).number)
        val again = f.import(zip("info.json" to info(name = "One v2"), "p.png" to "c"))
        assertEquals(1, again.number)
        assertEquals("c", File(again.dir, "p.png").readText())
        assertEquals(listOf("One v2", "Two"), f.list().map { it.skin.name })
    }

    @Test
    fun removeDeletesTheSkinsFolder() {
        val f = files()
        val s = f.import(zip("info.json" to info(), "p.png" to "a"))
        f.remove(s.number)
        assertFalse(s.dir.exists())
        assertEquals(emptyList<SkinFiles.Imported>(), f.list())
        assertNull(f.get(1))
    }

    @Test
    fun eachProblemLeavesNothingBehind() {
        val f = files(maxBytes = 1000)
        assertEquals(SkinProblem.NOT_A_SKIN, problem(f, ByteArrayInputStream("not a zip at all".toByteArray())))
        assertEquals(SkinProblem.NOT_A_SKIN, problem(f, zip("readme.txt" to "hi")))
        assertEquals(SkinProblem.NOT_A_SKIN, problem(f, zip("info.json" to "{ broken")))
        assertEquals(SkinProblem.OTHER_CONSOLE, problem(f, zip("info.json" to info(type = "com.rileytestut.delta.game.gbc"), "p.png" to "a")))
        assertEquals(SkinProblem.PICTURE_MISSING, problem(f, zip("info.json" to info())))
        assertEquals(SkinProblem.NOT_A_SKIN, problem(f, zip("info.json" to info(image = "../p.png"), "p.png" to "a")))
        assertEquals(SkinProblem.NOT_A_SKIN, problem(f, zip("info.json" to info(), "p.png" to "a", "../evil.txt" to "x")))
        assertEquals(SkinProblem.TOO_BIG, problem(f, zip("info.json" to info(), "p.png" to "x".repeat(2000))))
        assertEquals(SkinProblem.TOO_BIG, problem(f, zip(*Array(SkinFiles.MAX_FILES + 1) { "f$it.txt" to "" })))
        assertEquals(emptyList<SkinFiles.Imported>(), f.list())
        assertFalse(File(tmp.root, "skins/.incoming").exists())
        assertFalse(File(tmp.root, "skins/evil.txt").exists())
        assertFalse(File(tmp.root, "evil.txt").exists())
    }

    @Test
    fun anUnreadableFolderIsLeftOutAndItsNumberIsNotReused() {
        File(tmp.root, "skins/7").mkdirs()
        File(tmp.root, "skins/7/info.json").writeText("{ broken")
        val f = files()
        assertEquals(emptyList<SkinFiles.Imported>(), f.list())
        assertNull(f.get(7))
        assertEquals(8, f.import(zip("info.json" to info(), "p.png" to "a")).number)
    }
}
```

- [x] **Step 2: Run it to make sure it fails**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -5`
Expected: FAIL — `Unresolved reference 'SkinFiles'`.

- [x] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/SkinFiles.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.util.zip.ZipInputStream

/**
 * The imported Delta skins (9), each in `files/skins/<n>/` (n = 1, 2, …; never reused): its `info.json`, its pictures,
 * `thumb.png` (the Skin page's tile picture, made by [thumbnail]) and kept PDF drawings (SkinPicture). An import is
 * unpacked into `skins/.incoming`, checked, then renamed into place, so a failure leaves nothing behind. Plain java.io,
 * so it is tested on the PC.
 */
class SkinFiles(filesDir: File, private val thumbnail: (dir: File, face: DeltaSkin.Face, out: File) -> Unit = { _, _, _ -> },
                private val maxBytes: Long = MAX_BYTES) {
    data class Imported(val number: Int, val dir: File, val skin: DeltaSkin) {
        val thumb: File get() = File(dir, THUMB)
    }

    val root = File(filesDir, "skins")

    /** Every readable imported skin, by number. */
    fun list(): List<Imported> = numbers().sorted().mapNotNull(::get)

    /** Skin [number], or null if it isn't there or can't be read. */
    fun get(number: Int): Imported? {
        val dir = File(root, number.toString())
        val info = File(dir, INFO)
        if (number <= 0 || !info.isFile) return null
        return try {
            Imported(number, dir, DeltaSkin.parse(info.readText()))
        } catch (e: Exception) {
            null
        }
    }

    fun remove(number: Int) {
        File(root, number.toString()).deleteRecursively()
    }

    /** Unpacks, checks and stores a `.deltaskin` (a zip); the same identifier replaces that copy. Throws [SkinException]. */
    fun import(input: InputStream): Imported {
        val incoming = File(root, ".incoming")
        incoming.deleteRecursively()
        try {
            unpack(input, incoming)
            val base = base(incoming) ?: throw SkinException(SkinProblem.NOT_A_SKIN)
            val skin = DeltaSkin.parse(File(base, INFO).readText())
            for (face in listOfNotNull(skin.upright, skin.sideways)) {
                if (!safe(face.image)) throw SkinException(SkinProblem.NOT_A_SKIN)
                if (!File(base, face.image).isFile) throw SkinException(SkinProblem.PICTURE_MISSING)
            }
            try {
                (skin.upright ?: skin.sideways)?.let { thumbnail(base, it, File(base, THUMB)) }
            } catch (e: Exception) { // no tile picture: the tile shows the name only
            }
            val number = list().firstOrNull { it.skin.identifier == skin.identifier }?.number ?: ((numbers().maxOrNull() ?: 0) + 1)
            val target = File(root, number.toString())
            target.deleteRecursively()
            if (!base.renameTo(target)) throw IOException("couldn't move the skin into place")
            return Imported(number, target, skin)
        } catch (e: IOException) { // a damaged zip, a full disk
            throw SkinException(SkinProblem.NOT_A_SKIN)
        } finally {
            incoming.deleteRecursively()
        }
    }

    private fun numbers(): List<Int> = (root.listFiles() ?: emptyArray()).mapNotNull { it.name.toIntOrNull() }.filter { it > 0 }

    private fun unpack(input: InputStream, into: File) {
        var files = 0
        var bytes = 0L
        val buffer = ByteArray(64 * 1024)
        ZipInputStream(input.buffered()).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                val name = entry.name.replace('\\', '/')
                if (!safe(name)) throw SkinException(SkinProblem.NOT_A_SKIN)
                if (entry.isDirectory) continue
                if (++files > MAX_FILES) throw SkinException(SkinProblem.TOO_BIG)
                val out = File(into, name)
                out.parentFile?.mkdirs()
                out.outputStream().use { o ->
                    while (true) {
                        val n = zip.read(buffer)
                        if (n < 0) break
                        bytes += n
                        if (bytes > maxBytes) throw SkinException(SkinProblem.TOO_BIG)
                        o.write(buffer, 0, n)
                    }
                }
            }
        }
        if (files == 0) throw SkinException(SkinProblem.NOT_A_SKIN) // not a zip, or an empty one
    }

    companion object {
        const val INFO = "info.json"
        const val THUMB = "thumb.png"
        const val MAX_FILES = 500
        const val MAX_BYTES = 50L * 1024 * 1024

        /** A path inside the zip that stays inside the skin's folder. */
        fun safe(name: String) = name.isNotEmpty() && !name.startsWith("/") && ':' !in name && name.split('/').none { it == ".." }

        /** Where `info.json` is: at the top, or inside the one top folder (a Mac's `__MACOSX` folder aside). */
        private fun base(dir: File): File? {
            if (File(dir, INFO).isFile) return dir
            val tops = (dir.listFiles() ?: return null).filter { it.isDirectory && it.name != "__MACOSX" }
            return tops.singleOrNull()?.takeIf { File(it, INFO).isFile }
        }
    }
}
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3` then the count command.
Expected: BUILD SUCCESSFUL; `118 0`.

- [x] **Step 5: Commit** (with `docs/PROGRESS.md` and this plan's ticks)

```bash
git add app/src/main/java/com/tengen/gbaemu/SkinFiles.kt app/src/test/java/com/tengen/gbaemu/SkinFilesTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-9-delta-skins.md
git commit -m "feat(app): importing, listing and removing Delta skins

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The imported-skin setting and the Quick slot

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Settings.kt` (`skinImport`, `touch_skin_import`)
- Modify: `app/src/main/java/com/tengen/gbaemu/States.kt` (`QUICK`, `name`)
- Test: `app/src/test/java/com/tengen/gbaemu/SettingsTest.kt` (+1 test, 2 extended), `app/src/test/java/com/tengen/gbaemu/StatesTest.kt` (extended)

- [x] **Step 1: Write the failing tests**

In `SettingsTest.kt`: in `nothingStoredMeansTheDefaultsWhichAreTodaysBehaviour` add after `assertEquals(0, s.skin) // Plain`:

```kotlin
        assertEquals(0, s.skinImport) // a built-in skin
```

In `everySettingIsSavedAndReadBack` change `showSpeed = true, skin = 12)` to `showSpeed = true, skin = 12, skinImport = 3)`. Add the test:

```kotlin
    @Test
    fun anImportedSkinIsKeptByItsNumberAndANegativeNumberIsIgnored() {
        val store = MemoryStore()
        Settings(skinImport = 3).save(store)
        assertEquals(3, Settings.load(store).skinImport)
        store.put("touch_skin_import", -1)
        assertEquals(0, Settings.load(store).skinImport)
    }
```

In `StatesTest.filesPerGameAndSlot` add at the end:

```kotlin
        assertEquals(File("files/states/1a2b3c4d/quick.state"), s.state(States.QUICK))
        assertEquals(File("files/states/1a2b3c4d/quick.png"), s.picture(States.QUICK))
```

- [x] **Step 2: Run them to make sure they fail**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -5`
Expected: FAIL — `No parameter with name 'skinImport'` / `Unresolved reference 'QUICK'`.

- [x] **Step 3: Write the code**

`Settings.kt`: after `val skin: Int = 0, // ...` add

```kotlin
    val skinImport: Int = 0, // an imported Delta skin (9): its folder number in files/skins; 0 = the built-in [skin]
```

in `save` after `store.put("touch_skin", skin)`:

```kotlin
        store.put("touch_skin_import", skinImport)
```

in `load` after the `skin = ...` line:

```kotlin
                skinImport = store.get("touch_skin_import")?.takeIf { it >= 0 } ?: d.skinImport,
```

`States.kt`: the class comment's first line becomes `One game's save states in `<files>/states/<game id>/`: slots 1-10, the automatic one ([AUTO]) and the Quick one ([QUICK], a Delta skin's quick save, 9), each a`; in the companion:

```kotlin
        const val AUTO = 0
        const val SLOTS = 10
        const val QUICK = 11

        fun name(slot: Int): String = when (slot) {
            AUTO -> "auto"
            QUICK -> "quick"
            else -> "slot$slot"
        }
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3` then the count command.
Expected: BUILD SUCCESSFUL; `119 0`.

- [x] **Step 5: Commit** (with `docs/PROGRESS.md` and this plan's ticks)

```bash
git add app/src/main/java/com/tengen/gbaemu/Settings.kt app/src/main/java/com/tengen/gbaemu/States.kt app/src/test/java/com/tengen/gbaemu/SettingsTest.kt app/src/test/java/com/tengen/gbaemu/StatesTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-9-delta-skins.md
git commit -m "feat(app): the imported-skin setting and the Quick save slot

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Drawing a skin's pictures and the skin over the game

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/SkinPicture.kt` (`isPdf`, `make`, `kept`, `thumbnail`)
- Create: `app/src/main/java/com/tengen/gbaemu/DeltaSkinView.kt`
- Test: `app/src/androidTest/java/com/tengen/gbaemu/SkinPictureTest.kt` (new, 3 tests)

- [x] **Step 1: Write the failing test**

Create `app/src/androidTest/java/com/tengen/gbaemu/SkinPictureTest.kt`:

```kotlin
package com.tengen.gbaemu

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.Paint
import android.graphics.pdf.PdfDocument
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** A Delta skin's pictures (9): a PDF drawn by Android's PdfRenderer, a PNG decoded, the tile picture. */
@RunWith(AndroidJUnit4::class)
class SkinPictureTest {
    private val dir = File(InstrumentationRegistry.getInstrumentation().targetContext.cacheDir, "skinpicture").apply {
        deleteRecursively()
        mkdirs()
    }

    private fun png(name: String, w: Int, h: Int, colour: Int) {
        val b = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888).apply { eraseColor(colour) }
        File(dir, name).outputStream().use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    @Test
    fun aPdfIsDrawnAtTheAskedSizeSeeThroughWhereEmptyAndKept() {
        val pdf = File(dir, "p.pdf")
        PdfDocument().apply {
            val page = startPage(PdfDocument.PageInfo.Builder(100, 50, 1).create())
            page.canvas.drawRect(0f, 0f, 50f, 50f, Paint().apply { color = Color.RED }) // the left half red, the right empty
            finishPage(page)
            pdf.outputStream().use { writeTo(it) }
            close()
        }
        assertTrue(SkinPicture.isPdf(pdf))
        val bmp = SkinPicture.kept(dir, "p.pdf", sideways = false, 300, 150)!!
        assertEquals(300, bmp.width)
        assertEquals(150, bmp.height)
        assertEquals(Color.RED, bmp.getPixel(60, 75))
        assertEquals(0, Color.alpha(bmp.getPixel(240, 75)))
        val copy = File(dir, "cache-p-300x150.png")
        assertTrue(copy.isFile)
        assertEquals(Color.RED, BitmapFactory.decodeFile(copy.path).getPixel(60, 75))
    }

    @Test
    fun aPngIsDecodedNoSmallerThanNeededAndNotKept() {
        png("p.png", 800, 400, Color.BLUE)
        assertFalse(SkinPicture.isPdf(File(dir, "p.png")))
        val bmp = SkinPicture.kept(dir, "p.png", sideways = true, 300, 150)!!
        assertEquals(400, bmp.width) // 800 halved once; halved again (200) it would be smaller than 300
        assertEquals(Color.BLUE, bmp.getPixel(10, 10))
        assertFalse(File(dir, "cache-l-300x150.png").exists())
    }

    @Test
    fun theTilePictureIs240Wide() {
        png("p.png", 800, 400, Color.GREEN)
        val out = File(dir, "thumb.png")
        SkinPicture.thumbnail(dir, DeltaSkin.Face("p.png", 400f, 200f, emptyList(), null, false), out)
        val t = BitmapFactory.decodeFile(out.path)
        assertEquals(240, t.width)
        assertEquals(120, t.height)
        assertEquals(Color.GREEN, t.getPixel(100, 60))
    }
}
```

- [x] **Step 2: Run it to make sure it fails**

Run: `bash tools/dev.sh apk 2>&1 | tail -3 && bash tools/dev.sh androidtest 2>&1 | tail -5`
Expected: the test build fails — `Unresolved reference 'SkinPicture'`.

- [x] **Step 3: Write `SkinPicture.kt`**

```kotlin
package com.tengen.gbaemu

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.graphics.pdf.PdfRenderer
import android.os.ParcelFileDescriptor
import java.io.File
import java.io.IOException
import kotlin.math.roundToInt

/**
 * A Delta skin's picture as a bitmap at the size it is drawn (9): a PDF's first page drawn by Android's PdfRenderer
 * (sharp at any size, see-through where the PDF is empty), a PNG decoded. Slow (a PDF up to ~0.7 s): call it off the
 * UI thread. PDF or PNG is told by the file's first bytes, not its name.
 */
object SkinPicture {
    fun isPdf(file: File): Boolean = try {
        file.inputStream().use { s ->
            val head = ByteArray(4)
            s.read(head) == 4 && String(head, Charsets.ISO_8859_1) == "%PDF"
        }
    } catch (e: IOException) {
        false
    }

    /** [file] at [w] × [h]: a PDF exactly, a PNG decoded no smaller than that (halved while it stays big enough); null if unreadable. */
    fun make(file: File, w: Int, h: Int): Bitmap? = try {
        if (isPdf(file)) pdf(file, w, h) else png(file, w, h)
    } catch (e: Exception) { // damaged, or too big for memory
        null
    }

    /** As [make]; a PDF's drawing is kept beside it (`cache-<p|l>-<w>x<h>.png`) and read from there next time. */
    fun kept(dir: File, image: String, sideways: Boolean, w: Int, h: Int): Bitmap? {
        val file = File(dir, image)
        if (!isPdf(file)) return make(file, w, h) // a PNG decodes faster than a kept copy would be written
        val copy = File(dir, "cache-${if (sideways) "l" else "p"}-${w}x$h.png")
        if (copy.isFile) BitmapFactory.decodeFile(copy.path)?.let { return it }
        val bmp = make(file, w, h) ?: return null
        try {
            copy.outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        } catch (e: IOException) {
            copy.delete()
        }
        return bmp
    }

    /** The Skin page's tile picture: [face]'s picture 240 px wide, as a PNG in [out]. */
    fun thumbnail(dir: File, face: DeltaSkin.Face, out: File) {
        val w = 240
        val h = (w * face.height / face.width).roundToInt().coerceAtLeast(1)
        val bmp = make(File(dir, face.image), w, h) ?: return
        val tile = if (bmp.width == w && bmp.height == h) bmp else Bitmap.createScaledBitmap(bmp, w, h, true)
        out.outputStream().use { tile.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    private fun pdf(file: File, w: Int, h: Int): Bitmap =
        ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY).use { fd ->
            PdfRenderer(fd).use { renderer ->
                renderer.openPage(0).use { page ->
                    val out = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
                    val m = Matrix().apply { setScale(w.toFloat() / page.width, h.toFloat() / page.height) }
                    page.render(out, null, m, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY)
                    out
                }
            }
        }

    private fun png(file: File, w: Int, h: Int): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(file.path, bounds)
        var sample = 1
        while (bounds.outWidth / (sample * 2) >= w && bounds.outHeight / (sample * 2) >= h) sample *= 2
        return BitmapFactory.decodeFile(file.path, BitmapFactory.Options().apply { inSampleSize = sample })
    }
}
```

- [x] **Step 4: Write `DeltaSkinView.kt`**

```kotlin
package com.tengen.gbaemu

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * A Delta skin (9) over the whole screen: its picture placed by [DeltaLayout] (made on a background thread by
 * [SkinPicture]; nothing of the skin is drawn until it is ready — [onPictureFailed] if it can't be), an opaque skin's
 * leftover strips in the colours of the picture's edges, and the items under a finger darkened. Fingers press the GBA
 * buttons ([onMask], as TouchControlsView) and fire actions ([onAction]), with a vibration tick unless [buzz] is off.
 * A translucent skin is see-through by [look] (Faint 40 %, Normal 70 % — Delta's own — Strong 100 %). While [hidden]
 * (a keyboard or gamepad in use) nothing is drawn; the next touch shows it again and presses nothing.
 */
class DeltaSkinView(context: Context) : View(context) {
    var skin: SkinFiles.Imported? = null
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }
    var sideways = false
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }
    var look = Settings.LOOK_NORMAL
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }
    var hidden = false
        set(value) {
            if (field == value) return
            field = value
            if (value) release()
            invalidate()
        }
    var buzz = true
    var onMask: (Int) -> Unit = {}
    var onAction: (DeltaSkin.Action) -> Unit = {}
    var onShow: () -> Unit = {}
    var onPictureFailed: () -> Unit = {}

    private var layout: DeltaLayout? = null
    private var touch: DeltaTouch? = null
    private var picture: Bitmap? = null
    private var edges: IntArray? = null // the picture's edge colours: DeltaLayout.TOP, BOTTOM, LEFT, RIGHT
    private var generation = 0 // a picture made for an older size or skin is dropped
    private var mask = 0
    private var ignoreUntilUp = false // the touch that showed the skin presses nothing
    private val paint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val fill = Paint()
    private val shade = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = SHADE }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) = relayout()

    private fun relayout() {
        release()
        picture = null
        edges = null
        val s = skin
        val face = s?.skin?.face(sideways)
        val l = if (face != null && width > 0 && height > 0) DeltaLayout(face, width.toFloat(), height.toFloat(), sideways) else null
        layout = l
        touch = l?.let(::DeltaTouch)
        val gen = ++generation
        if (s != null && l != null) {
            val w = (l.picture.right - l.picture.left).roundToInt()
            val h = (l.picture.bottom - l.picture.top).roundToInt()
            val side = sideways
            Thread {
                val bmp = SkinPicture.kept(s.dir, l.face.image, side, w, h)
                post {
                    if (gen != generation) return@post
                    if (bmp == null) {
                        onPictureFailed()
                    } else {
                        picture = bmp
                        edges = edgeColours(bmp)
                        invalidate()
                    }
                }
            }.start()
        }
        invalidate()
    }

    /** Nothing held (a new layout, or hidden). */
    private fun release() {
        touch?.update(emptyMap())
        setMask(0)
    }

    @SuppressLint("ClickableViewAccessibility") // game buttons, not clicks
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val t = touch ?: return false
        val action = event.actionMasked
        if (hidden) {
            if (action == MotionEvent.ACTION_DOWN) {
                hidden = false
                ignoreUntilUp = true
                onShow()
            }
            return true
        }
        if (ignoreUntilUp) {
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) ignoreUntilUp = false
            return true
        }
        val fingers = HashMap<Int, Pair<Float, Float>>()
        if (action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
            val lifted = if (action == MotionEvent.ACTION_POINTER_UP) event.actionIndex else -1
            for (i in 0 until event.pointerCount) {
                if (i != lifted) fingers[event.getPointerId(i)] = event.getX(i) to event.getY(i)
            }
        }
        val before = t.held.toSet()
        val fire = t.update(fingers)
        setMask(t.mask)
        if (t.held.toSet() != before) invalidate()
        if (fire.isNotEmpty() && buzz) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        fire.forEach(onAction)
        return true
    }

    private fun setMask(pressed: Int) {
        if (pressed == mask) return
        if (buzz && pressed and mask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        mask = pressed
        onMask(pressed)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        val l = layout ?: return
        val bmp = picture ?: return
        if (hidden) return
        if (!l.face.translucent) edges?.let { e ->
            for ((side, b) in l.leftovers()) {
                fill.color = e[side]
                canvas.drawRect(b.left, b.top, b.right, b.bottom, fill)
            }
        }
        paint.alpha = if (!l.face.translucent) 255 else when (look) {
            Settings.LOOK_FAINT -> 102
            Settings.LOOK_STRONG -> 255
            else -> 179
        }
        canvas.drawBitmap(bmp, Rect(0, 0, bmp.width, bmp.height),
            RectF(l.picture.left, l.picture.top, l.picture.right, l.picture.bottom), paint)
        for (item in touch?.held?.toSet() ?: emptySet()) {
            val b = l.box(item.frame)
            val w = b.right - b.left
            val h = b.bottom - b.top
            if (item.dpad) { // the pressed cells
                val cw = w / 3
                val ch = h / 3
                val r = cw * 0.15f
                fun cell(column: Int, row: Int) = canvas.drawRoundRect(b.left + column * cw, b.top + row * ch,
                    b.left + (column + 1) * cw, b.top + (row + 1) * ch, r, r, shade)
                if (mask and KeyMap.UP != 0) cell(1, 0)
                if (mask and KeyMap.DOWN != 0) cell(1, 2)
                if (mask and KeyMap.LEFT != 0) cell(0, 1)
                if (mask and KeyMap.RIGHT != 0) cell(2, 1)
            } else if (DeltaLayout.round(item.frame)) {
                canvas.drawOval(b.left, b.top, b.right, b.bottom, shade)
            } else {
                val r = min(w, h) / 2
                canvas.drawRoundRect(b.left, b.top, b.right, b.bottom, r, r, shade)
            }
        }
    }

    /** The middle pixel of each edge of the picture, opaque. */
    private fun edgeColours(b: Bitmap) = intArrayOf(
        b.getPixel(b.width / 2, 0), b.getPixel(b.width / 2, b.height - 1),
        b.getPixel(0, b.height / 2), b.getPixel(b.width - 1, b.height / 2),
    ).map { it or (0xFF shl 24) }.toIntArray()

    private companion object {
        const val SHADE = 0x40000000 // black at 25 %
    }
}
```

- [x] **Step 5: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3` (count `119 0`), then `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | tail -3` and the on-device count.
Expected: BUILD SUCCESSFUL; on-device 61, 0 failures.

- [x] **Step 6: Commit** (with `docs/PROGRESS.md` and this plan's ticks)

```bash
git add app/src/main/java/com/tengen/gbaemu/SkinPicture.kt app/src/main/java/com/tengen/gbaemu/DeltaSkinView.kt app/src/androidTest/java/com/tengen/gbaemu/SkinPictureTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-9-delta-skins.md
git commit -m "feat(app): drawing a Delta skin's pictures and the skin over the game

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: The Skin page — imported skins, Import, remove; the Edit layout rows

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/AppTheme.kt` (`NavRow(enabled)`)
- Modify: `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt` (`ImportedSkin`, `SettingsScreen`'s new parameters, the Touch buttons rows, `SkinGrid`, `ImportedTile`, `ImportTile`, `SkinErrorDialog`)
- Test: `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt` (+1 test)

- [x] **Step 1: Write the failing test**

In `ScreensTest.kt` add the imports `androidx.compose.ui.test.longClick`, `androidx.compose.ui.test.performScrollTo`, `androidx.compose.ui.test.performTouchInput`, and the test:

```kotlin
    @Test
    fun importedSkinsArePickedImportedAndRemovedAndTheirLayoutRowsAreOff() {
        var changed = Settings()
        var imported = false
        var removed = 0
        val edited = ArrayList<Boolean>()
        val page = mutableStateOf(SettingsPage.SKIN)
        val skins = listOf(ImportedSkin(2, "Bumble", null, upright = true, sideways = false))
        compose.setContent {
            AppTheme {
                SettingsScreen(page.value, Settings(skin = 5, skinImport = 2), KeyBindings(MemoryStore()), 0, "Built-in",
                    onPage = {}, onBack = {}, onChange = { changed = it }, onPickKey = { _, _ -> }, onResetKeys = {},
                    onChooseBios = {}, onBuiltInBios = {}, onEditLayout = { edited += it }, importedSkins = skins,
                    onImportSkin = { imported = true }, onRemoveSkin = { removed = it })
            }
        }
        compose.onNodeWithText("Bumble").performScrollTo().performClick()
        assertEquals(2, changed.skinImport)
        compose.onNodeWithText("Import").performScrollTo().performClick()
        assertEquals(true, imported)
        compose.onNodeWithText("Bumble").performScrollTo().performTouchInput { longClick() }
        compose.onNodeWithText("Remove Bumble?").assertIsDisplayed()
        compose.onNodeWithText("Remove").performClick()
        assertEquals(2, removed)
        compose.onNodeWithText("Ice").performScrollTo().performClick()
        assertEquals(1, changed.skin) // a built-in skin: the imported one is no longer chosen
        assertEquals(0, changed.skinImport)
        page.value = SettingsPage.TOUCH
        compose.onNodeWithText("Bumble").assertIsDisplayed() // the Skin row's summary
        compose.onNodeWithText("This skin sets its own layout").assertIsDisplayed() // upright: the skin covers it
        compose.onNodeWithText("Edit upright layout").performClick()
        compose.onNodeWithText("Edit sideways layout").performClick()
        assertEquals(listOf(true), edited) // only sideways opens
    }
```

- [x] **Step 2: Run it to make sure it fails**

Run: `bash tools/dev.sh androidtest 2>&1 | tail -5`
Expected: the test build fails — `Unresolved reference 'ImportedSkin'` / `No parameter with name 'importedSkins'`.

- [x] **Step 3: `NavRow(enabled)`**

In `AppTheme.kt` add `import androidx.compose.ui.draw.alpha` and change `NavRow`:

```kotlin
/** A row that opens a page; [enabled] false: greyed out, a tap does nothing. */
@Composable
fun NavRow(icon: ImageVector?, title: String, summary: String?, enabled: Boolean = true, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clickable(enabled = enabled, onClick = onClick).alpha(if (enabled) 1f else 0.38f)
        .heightIn(min = 56.dp).padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically) {
```

(the rest of `NavRow` unchanged; if `NavRow` already has a comment line above it, replace that line with the one above).

- [x] **Step 4: The Settings screen**

In `SettingsScreens.kt`:

Imports to add: `android.graphics.Bitmap`, `androidx.compose.foundation.ExperimentalFoundationApi`, `androidx.compose.foundation.Image`, `androidx.compose.foundation.combinedClickable`, `androidx.compose.foundation.layout.size`, `androidx.compose.material.icons.filled.Add`, `androidx.compose.material3.Icon`, `androidx.compose.ui.graphics.asImageBitmap`, `androidx.compose.ui.layout.ContentScale`, `androidx.compose.ui.text.style.TextOverflow`.

After `val LENGTH_SHORT = ...` add:

```kotlin
/** An imported Delta skin as the Skin page shows it (9): its number, name, tile picture and which directions it covers. */
data class ImportedSkin(val number: Int, val name: String, val thumb: Bitmap?, val upright: Boolean, val sideways: Boolean)
```

`SettingsScreen`'s parameters: after `onEditLayout: (sideways: Boolean) -> Unit = {}` add
`, importedSkins: List<ImportedSkin> = emptyList(), onImportSkin: () -> Unit = {}, onRemoveSkin: (Int) -> Unit = {}`; its comment gets the sentence ` [importedSkins]: the imported Delta skins (9); the chosen one ([Settings.skinImport]) sets the layout of the directions it covers.` At the top of the body, after `val s = settings`:

```kotlin
    val chosen = importedSkins.firstOrNull { it.number == s.skinImport } // an imported skin in use, or null
    val skinName = chosen?.name ?: Skins.at(s.skin).name
```

The main page's Touch buttons row: `"${Skins.at(s.skin).name} · ...` → `"$skinName · ...`. The Touch buttons page becomes:

```kotlin
                    SettingsPage.TOUCH -> {
                        NavRow(Icons.Filled.Palette, "Skin", skinName) { onPage(SettingsPage.SKIN) }
                        ChoiceRow("Sideways, over the picture", Settings.LOOK_NAMES, s.touchLook, { onChange(s.copy(touchLook = it)) },
                            help = "How strongly the buttons show over the game when the phone is sideways.")
                        SwitchRow("Buzz", s.buzz, { onChange(s.copy(buzz = it)) },
                            help = "Follows the phone's own touch-vibration setting.")
                        Spacer(Modifier.height(8.dp))
                        val own = "This skin sets its own layout"
                        val edit = "Move and size every button and the game picture"
                        NavRow(Icons.Filled.StayCurrentPortrait, "Edit upright layout", if (chosen?.upright == true) own else edit,
                            enabled = chosen?.upright != true) { onEditLayout(false) }
                        NavRow(Icons.Filled.StayCurrentLandscape, "Edit sideways layout", if (chosen?.sideways == true) own else edit,
                            enabled = chosen?.sideways != true) { onEditLayout(true) }
                    }
                    SettingsPage.SKIN -> {
                        SkinGrid(if (chosen != null) -1 else s.skin, importedSkins, chosen?.number,
                            onPick = { onChange(s.copy(skin = it, skinImport = 0)) },
                            onPickImported = { onChange(s.copy(skinImport = it)) }, onImport = onImportSkin, onRemove = onRemoveSkin)
                        Help("Import skins made for the Delta emulator (.deltaskin files). Press and hold an imported skin to remove it.")
                    }
```

Replace `SkinGrid` (keep `SkinTile` and `drawSkinPhone` as they are) with:

```kotlin
/** A tile of the Skin page: a built-in skin (its number), an imported one, or Import. */
private sealed interface Tile {
    data class BuiltIn(val number: Int) : Tile
    data class Imported(val skin: ImportedSkin) : Tile
    data object Import : Tile
}

/**
 * The Skin page (8b, 9): every built-in skin as a small phone in its colours, then the imported Delta skins (their
 * picture), then Import; the chosen one with an amber border. A tap picks it at once; pressing and holding an imported
 * skin asks to remove it. Rows of tiles (the page already scrolls): 3 across upright, more on a wide screen.
 */
@Composable
private fun SkinGrid(selected: Int, imported: List<ImportedSkin>, selectedImported: Int?, onPick: (Int) -> Unit,
                     onPickImported: (Int) -> Unit, onImport: () -> Unit, onRemove: (Int) -> Unit) {
    var removing by remember { mutableStateOf<ImportedSkin?>(null) }
    val tiles = Skins.ALL.indices.map { Tile.BuiltIn(it) } + imported.map { Tile.Imported(it) } + Tile.Import
    BoxWithConstraints(Modifier.fillMaxWidth().padding(top = 8.dp)) {
        val across = (maxWidth / 112.dp).toInt().coerceAtLeast(3)
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            tiles.chunked(across).forEach { row ->
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    for (tile in row) when (tile) {
                        is Tile.BuiltIn -> SkinTile(Skins.ALL[tile.number], tile.number == selected, { onPick(tile.number) },
                            Modifier.weight(1f))
                        is Tile.Imported -> ImportedTile(tile.skin, tile.skin.number == selectedImported,
                            { onPickImported(tile.skin.number) }, { removing = tile.skin }, Modifier.weight(1f))
                        Tile.Import -> ImportTile(onImport, Modifier.weight(1f))
                    }
                    repeat(across - row.size) { Spacer(Modifier.weight(1f)) }
                }
            }
        }
    }
    removing?.let { skin ->
        AlertDialog(onDismissRequest = { removing = null },
            title = { Text("Remove ${skin.name}?") },
            text = { Text("The skin is deleted from the app. You can import it again later.") },
            confirmButton = { TextButton(onClick = { removing = null; onRemove(skin.number) }) { Text("Remove") } },
            dismissButton = { TextButton(onClick = { removing = null }) { Text("Cancel") } })
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun ImportedTile(skin: ImportedSkin, chosen: Boolean, onClick: () -> Unit, onHold: () -> Unit, modifier: Modifier) {
    val shape = MaterialTheme.shapes.medium
    Surface(shape = shape, color = MaterialTheme.colorScheme.surfaceContainer,
        border = if (chosen) BorderStroke(2.dp, MaterialTheme.colorScheme.primary) else null,
        modifier = modifier.clip(shape).combinedClickable(onClick = onClick, onLongClick = onHold)) {
        Column(Modifier.padding(8.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Box(Modifier.fillMaxWidth().aspectRatio(0.5f).clip(RoundedCornerShape(8.dp)).background(Color.Black),
                contentAlignment = Alignment.Center) {
                skin.thumb?.let {
                    Image(it.asImageBitmap(), contentDescription = null, contentScale = ContentScale.Fit, modifier = Modifier.fillMaxSize())
                }
            }
            Text(skin.name, style = MaterialTheme.typography.labelLarge, maxLines = 1, overflow = TextOverflow.Ellipsis,
                modifier = Modifier.padding(top = 6.dp))
        }
    }
}

@Composable
private fun ImportTile(onClick: () -> Unit, modifier: Modifier) {
    Surface(onClick = onClick, shape = MaterialTheme.shapes.medium, color = MaterialTheme.colorScheme.surfaceContainer,
        modifier = modifier) {
        Column(Modifier.padding(8.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Box(Modifier.fillMaxWidth().aspectRatio(0.5f), contentAlignment = Alignment.Center) {
                Icon(Icons.Filled.Add, contentDescription = null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(36.dp))
            }
            Text("Import", style = MaterialTheme.typography.labelLarge, maxLines = 1, modifier = Modifier.padding(top = 6.dp))
        }
    }
}

/** Why an import failed (9): the message and OK. */
@Composable
fun SkinErrorDialog(message: String, onOk: () -> Unit) {
    AlertDialog(onDismissRequest = onOk, title = { Text("Skin not imported") }, text = { Text(message) },
        confirmButton = { TextButton(onClick = onOk) { Text("OK") } })
}
```

- [x] **Step 5: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3` (count `119 0`: `UiTextTest` reads the new words), then `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | tail -3` and the on-device count.
Expected: BUILD SUCCESSFUL; on-device 62, 0 failures.

- [x] **Step 6: Commit** (with `docs/PROGRESS.md` and this plan's ticks)

```bash
git add app/src/main/java/com/tengen/gbaemu/AppTheme.kt app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-9-delta-skins.md
git commit -m "feat(app): the Skin page with imported skins, Import and remove

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: The skin in the game, quick save / load, importing from the phone

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt` (`GameScreen` with a Delta skin; `SlotGrid` / `SlotCard` with Quick)
- Modify: `app/src/main/java/com/tengen/gbaemu/MainActivity.kt` (the skins, the picker, the actions, the error pop-up, the background)
- Test: `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt` (+1 test)

- [x] **Step 1: Write the failing test**

In `ScreensTest.kt` add the imports `androidx.test.platform.app.InstrumentationRegistry` and `java.io.File` (`assertDoesNotExist` is a member of the node: no import), and the test:

```kotlin
    @Test
    fun theLoadPageOffersTheQuickSlotFirstAndTheSavePageDoesNot() {
        val dir = File(InstrumentationRegistry.getInstrumentation().targetContext.cacheDir, "quickslot").apply { deleteRecursively() }
        val states = States(dir, "game")
        states.dir.mkdirs()
        states.state(States.QUICK).writeText("q")
        states.state(2).writeText("s")
        var picked = -1
        val saving = mutableStateOf(false)
        compose.setContent { AppTheme { SlotGrid(saving.value, states, 0, onPick = { picked = it }, onBack = {}) } }
        compose.onNodeWithText("Slot 2").assertIsDisplayed()
        compose.onNodeWithText("Quick").performClick()
        assertEquals(States.QUICK, picked)
        saving.value = true
        compose.onNodeWithText("Quick").assertDoesNotExist()
        compose.onNodeWithText("Slot 10").performScrollTo().assertIsDisplayed()
    }
```

- [x] **Step 2: Run it to make sure it fails**

Run: `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | tail -5` and the on-device count.
Expected: 1 failure (no "Quick" on the Load page) — 62 run, 1 failed.

- [x] **Step 3: `SlotGrid` with Quick**

In `Screens.kt` replace the start of `SlotGrid` (its comment and the lines up to `val slots = ...`) with:

```kotlin
/**
 * The 10 slots as a grid of pictures (two columns). Saving: every slot can be picked; loading: only used ones, the
 * Quick one (a Delta skin's quick save, 9) first. [refresh] changes whenever the slots may have changed, so the
 * pictures are read again.
 */
@Composable
fun SlotGrid(saving: Boolean, states: States, refresh: Int, onPick: (Int) -> Unit, onBack: () -> Unit) {
    val all = remember(saving) { (if (saving) emptyList() else listOf(States.QUICK)) + (1..States.SLOTS) }
    val used = remember(refresh, all) { all.associateWith { states.used(it) } }
    val pictures = remember(refresh, all) { all.associateWith { if (used.getValue(it)) states.loadPicture(it) else null } }
    val dates = remember(refresh, all) {
        val format = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
        all.associateWith { if (used.getValue(it)) format.format(Date(states.savedAt(it))) else null }
    }
    val slots = all.filter { saving || used.getValue(it) }
```

and in its loop `SlotCard(slot, pictures[slot - 1], dates[slot - 1], onPick, Modifier.weight(1f))` → `SlotCard(slot, pictures[slot], dates[slot], onPick, Modifier.weight(1f))`. In `SlotCard`, at the top of its body add `val name = if (slot == States.QUICK) "Quick" else "Slot $slot"`, then `contentDescription = "slot $slot"` → `contentDescription = name.lowercase()` and `Text("Slot $slot", ...` → `Text(name, ...`.

- [x] **Step 4: `GameScreen` with a Delta skin**

`GameScreen`'s parameters: after `fitMode: Int = Settings.SIZE_FIT` add
`, delta: SkinFiles.Imported? = null, onAction: (DeltaSkin.Action) -> Unit = {}, onSkinFailed: () -> Unit = {}`. Its body up to and including the touch buttons' `AndroidView` becomes:

```kotlin
    // The skin's body round the picture: here, and inside the game's surface by OpenGL (NativeBridge.setBackground);
    // black with a Delta skin (9).
    BoxWithConstraints(Modifier.fillMaxSize().background(if (delta != null) Color.Black else Color(skin.body))) {
        val sideways = maxWidth > maxHeight
        val edits = layouts.of(sideways)
        val density = LocalDensity.current
        val w = constraints.maxWidth.toFloat()
        val h = constraints.maxHeight.toFloat()
        // A Delta skin's layout for this direction (none, or its picture failed: the Plain buttons with the user's layout).
        var failed by remember(delta) { mutableStateOf(setOf<Boolean>()) }
        val face = if (sideways in failed) null else delta?.skin?.face(sideways)
        // The game's surface in the Delta skin's game box, or the layout's picture box (upright by default: full width
        // at the top; sideways: the whole screen, the game fitted inside); the buttons over the whole screen.
        val pic = if (face != null) DeltaLayout(face, w, h, sideways).game
            else TouchLayout(w, h, density.density, sideways, edits).picture
        AndroidView(
            modifier = Modifier.offset { IntOffset(pic.left.roundToInt(), pic.top.roundToInt()) }
                .requiredSize(with(density) { (pic.right - pic.left).toDp() }, with(density) { (pic.bottom - pic.top).toDp() }),
            factory = { gameSurface(it, onDrawFailed) },
        )
        if (face != null) AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { DeltaSkinView(it).also { onTouchMask(0) } }, // a new view (e.g. after turning): nothing held
            update = { v ->
                v.sideways = sideways
                v.skin = delta
                v.hidden = controlsHidden
                v.look = touchLook
                v.buzz = buzz
                v.onMask = onTouchMask
                v.onAction = onAction
                v.onShow = onShowControls
                v.onPictureFailed = {
                    failed = failed + sideways
                    onSkinFailed()
                }
            },
        ) else AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { TouchControlsView(it).also { onTouchMask(0) } }, // a new view (e.g. after turning): nothing held
            update = { v ->
                v.sideways = sideways
                v.edits = edits
                v.hidden = controlsHidden
                v.fastOn = fastOn
                v.look = touchLook
                v.skin = if (delta != null) Skins.PLAIN else skin
                v.fitMode = fitMode
                v.buzz = buzz
                v.onMask = onTouchMask
                v.onShow = onShowControls
            },
        )
```

(`Screens.kt` already imports `remember`, `mutableStateOf`, `getValue`, `setValue`; add any the compiler names as missing.)

- [x] **Step 5: `MainActivity`**

Fields, after `private var layouts by ...`:

```kotlin
    private lateinit var skinFiles: SkinFiles // the imported Delta skins (9)
    private var importedSkins by mutableStateOf(listOf<SkinFiles.Imported>())
    private var skinTiles by mutableStateOf(listOf<ImportedSkin>()) // as the Skin page shows them
    private var skinError by mutableStateOf<String?>(null) // why an import failed
```

A picker, after `importFor`:

```kotlin
    private val pickSkin = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importSkin(uri)
    }
```

In `onCreate`, between `fastSpeed = settings.fastSpeed` and `applySettings(settings)`:

```kotlin
        skinFiles = SkinFiles(filesDir, SkinPicture::thumbnail)
        refreshSkins()
        if (settings.skinImport != 0 && activeSkin() == null) { // its folder is gone or unreadable: the built-in skin
            settings = settings.copy(skinImport = 0)
            settings.save(settingsStore)
        }
```

`GameScreen(...)`: `skin = Skins.at(settings.skin),` stays; add after `fitMode = settings.size,`:

```kotlin
                        delta = activeSkin(),
                        onAction = ::skinAction,
                        onSkinFailed = { toast("This skin's picture could not be drawn") },
```

`SettingsScreen(...)`: after `onEditLayout = ::openLayoutEditor` add `, importedSkins = skinTiles, onImportSkin = { pickSkin.launch(arrayOf("*/*")) }, onRemoveSkin = ::removeSkin`. `LayoutEditor(...)`: `skin = Skins.at(settings.skin)` → `skin = if (activeSkin() != null) Skins.PLAIN else Skins.at(settings.skin)`. After the `unusualBios?.let { ... }` block add:

```kotlin
                skinError?.let { SkinErrorDialog(it, onOk = { skinError = null }) }
```

`applySettings`: `NativeBridge.setBackground(Skins.at(s.skin).body)` → `NativeBridge.setBackground(if (s.skinImport != 0) 0xFF000000.toInt() else Skins.at(s.skin).body)`.

New functions (after `loadFrom`):

```kotlin
    /** A Delta skin's button (9): Menu opens the pause menu; quick save / load use the Quick slot. */
    private fun skinAction(action: DeltaSkin.Action) = when (action) {
        DeltaSkin.Action.MENU -> openMenu()
        DeltaSkin.Action.QUICK_SAVE -> quickSave()
        DeltaSkin.Action.QUICK_LOAD -> quickLoad()
    }

    private fun quickSave() {
        val st = states ?: return
        NativeBridge.pause()
        val ok = st.save(States.QUICK)
        NativeBridge.resume()
        toast(if (ok) "Quick save done" else "Couldn't save the state.")
    }

    private fun quickLoad() {
        val st = states ?: return
        if (!st.used(States.QUICK)) return toast("No quick save yet")
        NativeBridge.pause()
        val ok = st.load(States.QUICK)
        NativeBridge.resume()
        if (!ok) toast("This state can't be loaded (damaged, or made by an older version).")
    }

    /** The chosen imported Delta skin, or null (a built-in skin). */
    private fun activeSkin(): SkinFiles.Imported? =
        if (settings.skinImport == 0) null else importedSkins.firstOrNull { it.number == settings.skinImport }

    private fun refreshSkins() {
        importedSkins = skinFiles.list()
        skinTiles = importedSkins.map {
            ImportedSkin(it.number, it.skin.name, it.thumb.takeIf(File::isFile)?.let { f -> BitmapFactory.decodeFile(f.path) },
                upright = it.skin.upright != null, sideways = it.skin.sideways != null)
        }
    }

    /** Imports the picked `.deltaskin` off the UI thread (a PDF tile picture takes a moment); a good one is chosen. */
    private fun importSkin(uri: Uri) {
        Thread {
            val result: Any = try {
                contentResolver.openInputStream(uri)?.use { skinFiles.import(it) } ?: SkinException(SkinProblem.NOT_A_SKIN)
            } catch (e: SkinException) {
                e
            } catch (e: Exception) { // the file can't be opened
                SkinException(SkinProblem.NOT_A_SKIN)
            }
            runOnUiThread {
                refreshSkins()
                when (result) {
                    is SkinFiles.Imported -> changeSettings(settings.copy(skinImport = result.number))
                    is SkinException -> skinError = result.problem.message
                }
            }
        }.start()
    }

    private fun removeSkin(number: Int) {
        skinFiles.remove(number)
        refreshSkins()
        if (settings.skinImport == number) changeSettings(settings.copy(skinImport = 0))
    }
```

(`MainActivity` already imports `File`, `Uri`, `BitmapFactory` if used elsewhere; add any the compiler names as missing.)

- [x] **Step 6: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3` (count `119 0`), then `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | tail -3` and the on-device count.
Expected: BUILD SUCCESSFUL; on-device 63, 0 failures.

- [x] **Step 7: Commit** (with `docs/PROGRESS.md` and this plan's ticks)

```bash
git add app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-9-delta-skins.md
git commit -m "feat(app): Delta skins in the game, quick save and load, importing a skin

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Claude's check, the user's try, milestone 9 exit

**Files:** docs only (`docs/PROGRESS.md`, this plan, the spec's status, the parent spec's status line and §10, `CLAUDE.md` key files).

- [x] **Step 1: Full runs**

```bash
bash tools/dev.sh test 2>&1 | tail -3
bash tools/dev.sh jvmtest 2>&1 | tail -3
rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | tail -3
```

Expected: CTest `100% tests passed, 0 tests failed out of 120`; Kotlin `119 0`; on-device 63, 0 failures.

- [x] **Step 2: The test files on the emulator's Download folder**

Make the non-GBA copy and a plain zip in the scratchpad (never in the repo), then push all four:

```bash
S="<scratchpad>"; mkdir -p "$S/gbc" && unzip -o -q tests/skins/Standard.deltaskin -d "$S/gbc"
sed -i 's/com.rileytestut.delta.game.gba/com.rileytestut.delta.game.gbc/' "$S/gbc/info.json"
(cd "$S/gbc" && zip -q -r "$S/Other console.deltaskin" .) ; (cd tests/skins/r3bel && zip -q -r "$S/R3BEL Bumble.deltaskin" .)
echo hello > "$S/notes.txt" && (cd "$S" && zip -q "Plain zip.zip" notes.txt)
for f in tests/skins/Standard.deltaskin "$S/R3BEL Bumble.deltaskin" "$S/Other console.deltaskin" "$S/Plain zip.zip"; do bash tools/dev.sh push "$f"; done
```

(If `zip` is missing in Git Bash, build the zips with `python -c "import shutil; shutil.make_archive(...)"` and rename to `.deltaskin`.)

- [x] **Step 3: Claude's check on the emulator** (`bash tools/dev.sh install`; back up first: `adb shell settings get system accelerometer_rotation` / `user_rotation`, and `run-as com.tengen.gbaemu cat shared_prefs/settings.xml` saved to the scratchpad; put both back at the end). With screenshots (`adb exec-out screencap -p`) and taps (`adb shell input tap|motionevent`):
  1. Settings → Touch buttons → Skin: the Import tile; Import → the file picker → `Standard.deltaskin`: a new tile with its picture, chosen (amber border); Touch buttons shows its name and "This skin sets its own layout" on both Edit rows.
  2. `m7_demo` upright: the purple picture at the bottom, sharp; the game centred above; tap A, B, L, R, Start, Select, each D-pad side and a corner (two directions darkened); Menu opens the pause menu; Resume.
  3. Sideways: the see-through picture over the game, at Faint, Normal and Strong (Settings from the pause menu).
  4. Import `R3BEL Bumble.deltaskin`: upright, the game exactly in the painted screen, the top strip yellow; Load before any quick save → "No quick save yet"; Save → "Quick save done"; the Load page shows Quick first; quick Load works; FFW tap (fast, stays on), hold (fast only while held); sideways: the left and right strips yellow.
  5. Import `Other console.deltaskin` → "This skin is for another console. Only GBA skins work."; `Plain zip.zip` → "This file isn't a skin."; the tiles unchanged.
  6. Import `Standard.deltaskin` again: still one tile for it (replaced, same place).
  7. Press and hold the R3BEL tile → "Remove -- R3BEL ...?" → Remove: the tile is gone, Standard (or the built-in skin) in use.
  8. Pick Ice: the 8b skin and the user's saved layout are back; Edit layout rows enabled.
  Anything wrong → superpowers:systematic-debugging, fix test-first, commit, re-check.

- [x] **Step 4: The user's try (stop here)**

Tell the user (layman mode) how to try it on the phone emulator: Settings → Touch buttons → Skin → Import, pick a skin from Download (the two test skins are there, or one of their own), play a game with it upright and sideways, try Menu and the quick buttons. Wait for their answer; fix anything they report (test-first).

- [x] **Step 5: Docs, tag and the merge question**

After the user is happy: the spec's status → implemented (milestone 9 complete, the user's try passed); the parent spec's status line and §10 → milestone 9 ✅; `CLAUDE.md` key files → milestone 10 needs brainstorming (`.cht` import, cloud sync, box art), last finished: this spec and plan; `docs/PROGRESS.md` → Completed (each task's hash and notes), Project status (9 ✅), Next (brainstorm milestone 10 on a new branch `milestone-10`); every box in this plan ticked. Check every document for anything stale. Commit (`docs: milestone 9 complete - Delta skins`), `git tag milestone-9`, then superpowers:finishing-a-development-branch (the user picks merge / PR / keep).
