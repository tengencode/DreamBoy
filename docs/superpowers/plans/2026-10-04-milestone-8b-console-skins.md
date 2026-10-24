# Milestone 8b — Console Skins — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** 13 skins in the colours of the special-edition handhelds — the body round the game picture and the touch buttons — picked once for all games in Settings → Touch buttons → Skin; Plain (today's look) is the default.

**Architecture:** The skins are plain Kotlin data (`Skin.kt`: colours + the pressed / rim / contrast maths, tested on the PC). The touch buttons view draws from the chosen skin; the body colour comes from the game screen's background and, inside the game's surface, from OpenGL's clear colour (`NativeBridge.setBackground` → `jni_bridge.c` → the render thread → `video_gl_draw`). `TouchLayout` learns where the game is really drawn (the size mode), so sideways only the buttons over the game itself are see-through. The emulator core is not touched.

**Tech Stack:** Kotlin, Jetpack Compose (Material 3 1.4.0, material-icons-extended 1.7.8), C11 + OpenGL ES 3.0 (NDK 27), JUnit 4, Compose UI test 1.11.4, Gradle `--offline`, Git Bash `tools/dev.sh`.

**Spec:** `docs/superpowers/specs/2026-10-04-milestone-8b-console-skins-design.md`

---

## Conventions

- Git Bash from the repo root, on the branch `milestone-8`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest>` (the Pixel 9 phone emulator `Pixel_9_API_36_1`; never the user's USB phones unless the user asks). Gradle runs `--offline`; nothing new is downloaded. A missing artifact → stop and ask the user. Ask before restarting or cold-booting the emulator.
- The core is not touched: CTest stays 120/120 (run once in Task 6). The PC's unit tests don't build the OpenGL files; they are checked by the app build (0 C warnings: `touch platform/android/*.c && bash tools/dev.sh apk 2>&1 | grep -c "warning:"` → `0`) and the on-device tests.
- Counting tests: Kotlin unit tests `ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'` (total, failures); on-device `grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml` after `rm -rf app/build/outputs/androidTest-results`.
- Test-first: each task's tests are written first and fail (they don't compile without the new code), then the code. The drawing itself (Task 5) is checked by Claude on the emulator in Task 6.
- One commit per task; messages end with the Co-Authored-By trailer. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes). The tasks run one after another without stopping; **there is no user check in 8b** (user decision 2026-10-04): it ends with Claude's check on the emulator (Task 6) and the merge question. After every task: all documents updated and checked for anything stale.
- This plan's code is the prototype's (worktree `<scratchpad>/p8b`, branch `proto-8b`, `75affec`), checked by Claude on the emulator on 2026-10-04. After Task 5, `git diff proto-8b -- app platform` is empty. Kotlin: 88 → 94 (Task 1) → 94 (Task 2) → 96 (Task 4); on-device: 56 → 57 (Task 3) → 58 (Task 5).

## File map

```
app/src/main/java/com/tengen/gbaemu/Skin.kt (new); app/src/test/java/com/tengen/gbaemu/SkinTest.kt (new)                                    Task 1
app/src/main/java/com/tengen/gbaemu/Settings.kt; app/src/test/java/com/tengen/gbaemu/SettingsTest.kt                                        Task 2
platform/android/video_gl.{c,h}, render_thread.{c,h}, gl_readback.{c,h}, jni_bridge.c;
  app/src/main/java/com/tengen/gbaemu/NativeBridge.kt; app/src/androidTest/java/com/tengen/gbaemu/PictureTest.kt                                   Task 3
app/src/main/java/com/tengen/gbaemu/TouchLayout.kt; app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt                                  Task 4
app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt, Screens.kt, LayoutEditor.kt, SettingsScreens.kt, MainActivity.kt;
  app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt                                                       Task 5
```

---
### Task 1: The skins

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/Skin.kt` (`Skin`: the colours of one skin; `Skins.ALL` — 13, Plain first, a skin's place in the list is its stored number — `PLAIN`, `at`, `byId`, `pressed`, `rimOf`, `mix`, `luminance`, `contrast`)
- Test: `app/src/test/java/com/tengen/gbaemu/SkinTest.kt` (new, 6 tests)

- [x] **Step 1: Write the failing test**

Create `app/src/test/java/com/tengen/gbaemu/SkinTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

class SkinTest {
    @Test
    fun thirteenSkinsWithTheirOwnIdsAndNamesPlainFirst() {
        assertEquals(13, Skins.ALL.size)
        assertEquals(13, Skins.ALL.map { it.id }.toSet().size)
        assertEquals(13, Skins.ALL.map { it.name }.toSet().size)
        assertSame(Skins.PLAIN, Skins.ALL[0])
        assertEquals(listOf("Plain", "Ice", "Pink", "Lime", "White", "Black", "Orange", "Yellow", "Gold", "Silver",
            "Red and cream", "Grey and red", "Classic grey"), Skins.ALL.map { it.name })
    }

    @Test
    fun anUnknownNumberOrIdIsPlain() {
        assertSame(Skins.PLAIN, Skins.at(-1))
        assertSame(Skins.PLAIN, Skins.at(13))
        assertSame(Skins.PLAIN, Skins.byId("purple"))
        assertEquals("ice", Skins.at(1).id)
        assertEquals("classic", Skins.byId("classic").id)
    }

    @Test
    fun plainIsTheLookBefore8b() {
        val p = Skins.PLAIN
        assertEquals(0xFF000000.toInt(), p.body)
        for (c in listOf(p.dpad, p.ab, p.shoulder, p.small)) {
            assertEquals(0xFFB4B4B4.toInt(), c)
            assertEquals(0xFF6E6E6E.toInt(), Skins.pressed(c))
        }
        for (c in listOf(p.abText, p.shoulderText, p.smallText)) assertEquals(0xFF222222.toInt(), c)
        assertEquals(0xFFFFFFFF.toInt(), p.overPicture)
        assertEquals(0xFFFFFFFF.toInt(), p.overPictureText)
        assertEquals(0xFFFFD54A.toInt(), p.lit)
        assertNull(p.rim)
        assertTrue(p.seeThroughBesideGame) // sideways, every button see-through as before 8b
        for (s in Skins.ALL.drop(1)) assertFalse(s.name, s.seeThroughBesideGame)
    }

    @Test
    fun everyLabelIsReadableOnItsButton() {
        for (s in Skins.ALL) {
            for ((what, button, text) in listOf(Triple("A/B", s.ab, s.abText), Triple("L/R", s.shoulder, s.shoulderText),
                Triple("Select/Start", s.small, s.smallText), Triple("lit", s.lit, s.litText))) {
                val c = Skins.contrast(button, text)
                assertTrue("${s.name} $what: $c", c >= 4.5)
            }
        }
    }

    @Test
    fun everyButtonHasAnEdgeOnTheBodyAndLitShowsAgainstIdle() {
        for (s in Skins.ALL.drop(1)) {
            val rim = s.rim!!
            assertTrue("${s.name} rim", Skins.contrast(rim, s.body) >= 2.0)
            assertTrue("${s.name} lit", Skins.contrast(s.lit, s.shoulder) >= 2.0)
        }
    }

    @Test
    fun pressedIsDarkerOrForADarkColourLighter() {
        assertEquals(0xFF6E6E6E.toInt(), Skins.pressed(0xFFB4B4B4.toInt()))
        assertTrue(Skins.luminance(Skins.pressed(0xFFDFE3E8.toInt())) < Skins.luminance(0xFFDFE3E8.toInt()))
        assertTrue(Skins.luminance(Skins.pressed(0xFF0E0E10.toInt())) > Skins.luminance(0xFF0E0E10.toInt()))
    }
}
```

- [x] **Step 2: Run it to see it fail**

```bash
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: " | head -2
```

Expected: FAIL to compile — `Unresolved reference 'Skins'`.

- [x] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/Skin.kt`:

```kotlin
package com.tengen.gbaemu

import kotlin.math.pow
import kotlin.math.roundToInt

/**
 * A skin (8b): the colours of a handheld for the game screen — [body] round the game picture, the D-pad and the buttons
 * (shoulder = L, R, rewind and fast-forward; small = Select and Start), each with its label colour, and [lit] for
 * fast-forward while it is switched on. [rim]: an edge round every button on the body (null = none). [overPicture] /
 * [overPictureText]: the see-through buttons over the picture (null = each part's own colours). [seeThroughBesideGame]:
 * buttons beside the game but inside the picture's box (sideways: on the side bars) are see-through too — Plain, as
 * before 8b; the skins draw them solid on the body. Colours are ARGB. Pure Kotlin, so it is tested on the PC.
 */
data class Skin(
    val id: String, val name: String,
    val body: Int,
    val dpad: Int,
    val ab: Int, val abText: Int,
    val shoulder: Int, val shoulderText: Int,
    val small: Int, val smallText: Int,
    val lit: Int, val litText: Int,
    val rim: Int?,
    val overPicture: Int?, val overPictureText: Int?,
    val seeThroughBesideGame: Boolean = false,
)

object Skins {
    private const val DARK_LIT = 0xFF2A1A02.toInt() // the app's text-on-amber
    private const val AMBER = 0xFFF2A33A.toInt() // the app's amber (AppTheme)

    /** Plain: the touch buttons as before 8b — grey on black, see-through white over the picture. */
    val PLAIN = Skin("plain", "Plain", body = 0xFF000000.toInt(), dpad = 0xFFB4B4B4.toInt(),
        ab = 0xFFB4B4B4.toInt(), abText = 0xFF222222.toInt(), shoulder = 0xFFB4B4B4.toInt(), shoulderText = 0xFF222222.toInt(),
        small = 0xFFB4B4B4.toInt(), smallText = 0xFF222222.toInt(), lit = 0xFFFFD54A.toInt(), litText = 0xFF222222.toInt(),
        rim = null, overPicture = 0xFFFFFFFF.toInt(), overPictureText = 0xFFFFFFFF.toInt(), seeThroughBesideGame = true)

    /** A one-colour handheld: every button [button] with [text], a dark or amber [lit]. */
    private fun skin(id: String, name: String, body: Long, dpad: Long, button: Long, text: Long, amberLit: Boolean = false) =
        skin(id, name, body, dpad, button, text, button, text, button, text, amberLit)

    private fun skin(id: String, name: String, body: Long, dpad: Long, ab: Long, abText: Long, shoulder: Long,
                     shoulderText: Long, small: Long, smallText: Long, amberLit: Boolean) = Skin(
        id, name, body.toInt(), dpad.toInt(), ab.toInt(), abText.toInt(), shoulder.toInt(), shoulderText.toInt(),
        small.toInt(), smallText.toInt(),
        lit = if (amberLit) AMBER else DARK_LIT, litText = if (amberLit) DARK_LIT else AMBER,
        rim = rimOf(body.toInt()), overPicture = null, overPictureText = null)

    /**
     * Every skin, in the order of the Skin page. A skin's place in this list is its number in the settings
     * (`touch_skin`): new skins are only ever added at the end.
     */
    val ALL: List<Skin> = listOf(
        PLAIN,
        skin("ice", "Ice", 0xFFA9D3EA, 0xFF3B3E45, 0xFFDFE3E8, 0xFF333333),
        skin("pink", "Pink", 0xFFE892C0, 0xFF4A3A44, 0xFFF6D9E8, 0xFF5A2A44),
        skin("lime", "Lime", 0xFF8FD07A, 0xFF2E4A2A, 0xFFE6F4DF, 0xFF2E4A2A),
        skin("white", "White", 0xFFEEF0F2, 0xFF2F3136, 0xFFC9CCD2, 0xFF333333),
        skin("black", "Black", 0xFF26272C, 0xFF0E0E10, 0xFF4A4C52, 0xFFDDDDDD, amberLit = true),
        skin("orange", "Orange", 0xFFF08A2C, 0xFF3A2A1E, 0xFFFDE0BF, 0xFF5A3510),
        skin("yellow", "Yellow", 0xFFF3CC2E, 0xFF3A3320, 0xFFFFF2B8, 0xFF5A4A10),
        skin("gold", "Gold", 0xFFD2AE55, 0xFF3A2F14, 0xFFF2E3B0, 0xFF5A4410),
        skin("silver", "Silver", 0xFFB9BDC4, 0xFF3A3D44, 0xFFE9ECF0, 0xFF333333),
        skin("redcream", "Red and cream", 0xFFEBE2CD, 0xFF2B2B2B, 0xFFA61F2A, 0xFFFFFFFF, 0xFFA61F2A, 0xFFFFFFFF,
            0xFF8D1B22, 0xFFFFFFFF, amberLit = true),
        skin("greyred", "Grey and red", 0xFFC9C9C9, 0xFF1E1E1E, 0xFFC8202A, 0xFFFFFFFF, 0xFF2B2B2B, 0xFFDDDDDD,
            0xFF2B2B2B, 0xFFDDDDDD, amberLit = true),
        skin("classic", "Classic grey", 0xFFC4C0B8, 0xFF222222, 0xFF9A2257, 0xFFFFFFFF, 0xFF8F8C86, 0xFF222222,
            0xFF8A8A8A, 0xFF222222, amberLit = false),
    )

    /** The skin with this number (its place in [ALL]); anything else is Plain. */
    fun at(number: Int): Skin = ALL.getOrNull(number) ?: PLAIN

    fun byId(id: String): Skin = ALL.firstOrNull { it.id == id } ?: PLAIN

    /** A part held down: 39 % toward black; a dark colour 25 % toward white instead (Plain: B4B4B4 → 6E6E6E). */
    fun pressed(c: Int): Int = if (luminance(c) < 0.1) mix(c, 0xFFFFFFFF.toInt(), 0.25f) else mix(c, 0xFF000000.toInt(), 0.39f)

    /** The edge round the buttons: the body 40 % toward black; a dark body 30 % toward white. */
    fun rimOf(body: Int): Int = if (luminance(body) < 0.1) mix(body, 0xFFFFFFFF.toInt(), 0.30f) else mix(body, 0xFF000000.toInt(), 0.40f)

    /** [a] moved [t] of the way to [b], opaque. */
    fun mix(a: Int, b: Int, t: Float): Int {
        fun ch(c: Int, shift: Int) = (c shr shift) and 0xFF
        fun m(shift: Int) = (ch(a, shift) * (1 - t) + ch(b, shift) * t).roundToInt()
        return (0xFF shl 24) or (m(16) shl 16) or (m(8) shl 8) or m(0)
    }

    /** WCAG relative luminance (0 black .. 1 white). */
    fun luminance(c: Int): Double {
        fun lin(shift: Int): Double {
            val v = ((c shr shift) and 0xFF) / 255.0
            return if (v <= 0.03928) v / 12.92 else ((v + 0.055) / 1.055).pow(2.4)
        }
        return 0.2126 * lin(16) + 0.7152 * lin(8) + 0.0722 * lin(0)
    }

    /** WCAG contrast ratio, 1 (the same) .. 21 (black on white). */
    fun contrast(a: Int, b: Int): Double {
        val la = luminance(a)
        val lb = luminance(b)
        return (maxOf(la, lb) + 0.05) / (minOf(la, lb) + 0.05)
    }
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: |BUILD"
ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'
```

Expected: `BUILD SUCCESSFUL`; `94 0`.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Skin.kt app/src/test/java/com/tengen/gbaemu/SkinTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-8b-console-skins.md
git commit -m "feat(app): the 13 skins and their colour maths (Skin.kt)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: The skin setting

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Settings.kt` (`skin: Int = 0`, stored as `touch_skin`; an unknown number reads as Plain)
- Test: `app/src/test/java/com/tengen/gbaemu/SettingsTest.kt` (the existing tests extended: default 0, saved and read back, 13 reads as the default)

- [x] **Step 1: Write the failing test**

Replace `app/src/test/java/com/tengen/gbaemu/SettingsTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class SettingsTest {
    @Test
    fun nothingStoredMeansTheDefaultsWhichAreTodaysBehaviour() {
        val s = Settings.load(MemoryStore())
        assertEquals(Settings(), s)
        assertEquals(false, s.smooth)
        assertEquals(Settings.SIZE_FIT, s.size)
        assertEquals(true, s.sound)
        assertEquals(2, s.soundBursts)
        assertEquals(4, s.fastSpeed)
        assertEquals(64L * 1024 * 1024, s.rewindBudget)
        assertEquals(Settings.LOOK_NORMAL, s.touchLook)
        assertEquals(true, s.buzz)
        assertEquals(false, s.biosFile)
        assertEquals(false, s.showSpeed)
        assertEquals(0, s.skin) // Plain
    }

    @Test
    fun everySettingIsSavedAndReadBack() {
        val store = MemoryStore()
        val s = Settings(smooth = true, size = Settings.SIZE_STRETCH, sound = false, delay = Settings.DELAY_HIGH,
            fastSpeed = NativeBridge.FF_MAX, rewind = false, rewindLength = Settings.LENGTH_LONG,
            touchLook = Settings.LOOK_STRONG, buzz = false, biosFile = true, showSpeed = true, skin = 12)
        s.save(store)
        assertEquals(s, Settings.load(store))
    }

    @Test
    fun theFastForwardSpeedStoredBefore6bIsKept() {
        val store = MemoryStore()
        store.put("fast_speed", 8) // written by the in-game menu row since 5b
        assertEquals(8, Settings.load(store).fastSpeed)
    }

    @Test
    fun aValueThatDoesNotReadIsItsDefault() {
        val store = MemoryStore()
        for ((key, value) in listOf("size" to 9, "delay" to -2, "fast_speed" to 3, "rewind_length" to 7, "touch_look" to 5,
            "smooth" to 4, "touch_skin" to 13)) store.put(key, value)
        assertEquals(Settings(), Settings.load(store))
    }

    @Test
    fun theDelayAndTheRewindLengthMeanBurstsAndBytes() {
        assertEquals(listOf(2, 4, 8), listOf(Settings.DELAY_LOW, Settings.DELAY_MEDIUM, Settings.DELAY_HIGH)
            .map { Settings(delay = it).soundBursts })
        assertEquals(listOf(24L, 64L, 128L).map { it * 1024 * 1024 },
            listOf(Settings.LENGTH_SHORT, Settings.LENGTH_NORMAL, Settings.LENGTH_LONG).map { Settings(rewindLength = it).rewindBudget })
        assertEquals(0L, Settings(rewind = false).rewindBudget)
    }
}
```

- [x] **Step 2: Run it to see it fail**

```bash
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: " | head -2
```

Expected: FAIL to compile — `Unresolved reference 'skin'` (or `No parameter with name 'skin' found`).

- [x] **Step 3: Write the code**

Replace `app/src/main/java/com/tengen/gbaemu/Settings.kt`:

```kotlin
package com.tengen.gbaemu

/**
 * The app-wide settings (Settings screen). The defaults are the app's behaviour before 6b. Stored in an [IntStore]
 * (the "settings" preferences); `fast_speed` is the key the in-game menu row has used since 5b, so it is kept.
 */
data class Settings(
    val smooth: Boolean = false,
    val size: Int = SIZE_FIT,
    val sound: Boolean = true,
    val delay: Int = DELAY_LOW,
    val fastSpeed: Int = 4,
    val rewind: Boolean = true,
    val rewindLength: Int = LENGTH_NORMAL,
    val touchLook: Int = LOOK_NORMAL,
    val buzz: Boolean = true,
    val biosFile: Boolean = false, // a BIOS file was chosen (Bios.file); the next game starts through it
    val showSpeed: Boolean = false, // the frames per second label on the game screen (7a)
    val skin: Int = 0, // the touch buttons' skin (8b): its number in Skins.ALL; 0 = Plain
) {
    /** AAudio's own buffer, in bursts. */
    val soundBursts: Int get() = when (delay) {
        DELAY_MEDIUM -> 4
        DELAY_HIGH -> 8
        else -> 2
    }

    /** Bytes for rewind snapshots (0 = rewind off). */
    val rewindBudget: Long get() = if (!rewind) 0 else when (rewindLength) {
        LENGTH_SHORT -> 24L * MIB
        LENGTH_LONG -> 128L * MIB
        else -> 64L * MIB
    }

    fun save(store: IntStore) {
        store.put("smooth", flag(smooth))
        store.put("size", size)
        store.put("sound", flag(sound))
        store.put("delay", delay)
        store.put("fast_speed", fastSpeed)
        store.put("rewind", flag(rewind))
        store.put("rewind_length", rewindLength)
        store.put("touch_look", touchLook)
        store.put("buzz", flag(buzz))
        store.put("bios_file", flag(biosFile))
        store.put("show_speed", flag(showSpeed))
        store.put("touch_skin", skin)
    }

    companion object {
        private const val MIB = 1024 * 1024

        const val SIZE_FIT = 0 // the numbers of fit_mode_t (platform/android/fit_rect.h)
        const val SIZE_INTEGER = 1
        const val SIZE_STRETCH = 2
        const val DELAY_LOW = 0
        const val DELAY_MEDIUM = 1
        const val DELAY_HIGH = 2
        const val LENGTH_SHORT = 0
        const val LENGTH_NORMAL = 1
        const val LENGTH_LONG = 2
        const val LOOK_NORMAL = 0
        const val LOOK_FAINT = 1
        const val LOOK_STRONG = 2

        /** The choices' names, by value. */
        val SIZE_NAMES = listOf("Fit screen", "Whole-number steps", "Stretch to fill")
        val DELAY_NAMES = listOf("Low", "Medium", "High")
        val LENGTH_NAMES = listOf("About 1 minute", "About 2½ minutes", "About 5 minutes")
        val LOOK_NAMES = listOf("Normal", "Faint", "Strong")
        val SPEEDS = listOf(2, 4, 8, NativeBridge.FF_MAX)

        fun speedName(speed: Int) = if (speed == NativeBridge.FF_MAX) "Max" else "$speed×"

        fun load(store: IntStore): Settings {
            val d = Settings()
            return Settings(
                smooth = bool(store, "smooth", d.smooth),
                size = choice(store, "size", 0..2, d.size),
                sound = bool(store, "sound", d.sound),
                delay = choice(store, "delay", 0..2, d.delay),
                fastSpeed = store.get("fast_speed")?.takeIf { it in SPEEDS } ?: d.fastSpeed,
                rewind = bool(store, "rewind", d.rewind),
                rewindLength = choice(store, "rewind_length", 0..2, d.rewindLength),
                touchLook = choice(store, "touch_look", 0..2, d.touchLook),
                buzz = bool(store, "buzz", d.buzz),
                biosFile = bool(store, "bios_file", d.biosFile),
                showSpeed = bool(store, "show_speed", d.showSpeed),
                skin = choice(store, "touch_skin", Skins.ALL.indices, d.skin),
            )
        }

        private fun flag(b: Boolean) = if (b) 1 else 0

        private fun bool(store: IntStore, key: String, default: Boolean): Boolean = when (store.get(key)) {
            0 -> false
            1 -> true
            else -> default
        }

        private fun choice(store: IntStore, key: String, valid: IntRange, default: Int): Int =
            store.get(key)?.takeIf { it in valid } ?: default
    }
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: |BUILD"
ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'
```

Expected: `BUILD SUCCESSFUL`; `94 0`.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Settings.kt app/src/test/java/com/tengen/gbaemu/SettingsTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-8b-console-skins.md
git commit -m "feat(app): the skin setting (touch_skin)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: The body colour in the OpenGL drawing

**Files:**
- Modify: `platform/android/video_gl.h`, `platform/android/video_gl.c` (`video_gl_draw(..., uint32_t background)`: `glClearColor` from 0xAARRGGBB instead of black)
- Modify: `platform/android/render_thread.h`, `platform/android/render_thread.c` (`atomic_uint background`, black at start; `render_set_background`)
- Modify: `platform/android/gl_readback.h`, `platform/android/gl_readback.c` (the background passed through, for the test)
- Modify: `platform/android/jni_bridge.c` (`s_background`; `setBackground`, also sent when the render thread is made on start; `debugDrawFrameOn`, with `debugDrawFrameWith` drawing on black through it)
- Modify: `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt` (`setBackground(argb)`, `debugDrawFrameOn(...)`)
- Test: `app/src/androidTest/java/com/tengen/gbaemu/PictureTest.kt` (+1: `theBarsAreTheSkinsBodyColour`)

- [x] **Step 1: Write the failing test**

Replace `app/src/androidTest/java/com/tengen/gbaemu/PictureTest.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith

/** The real OpenGL drawing code, read back pixel by pixel. */
@RunWith(AndroidJUnit4::class)
class PictureTest {
    /** A frame where neighbouring pixels differ in every channel; white and black included. */
    private val frame = ShortArray(240 * 160) { i ->
        val x = i % 240
        val y = i / 240
        ((x * 7 + y * 3) and 31 or (((x + y * 5) and 31) shl 5) or (((x * 3 + y) and 31) shl 10)).toShort()
    }.also {
        it[0] = 0x7FFF
        it[1] = 0
    }

    /** BGR555 → the RGBA int glReadPixels gives: each 5-bit channel c becomes (c << 3) | (c >> 2). */
    private fun rgba(p: Short): Int {
        val v = p.toInt() and 0x7FFF
        fun ch(c: Int) = (c shl 3) or (c shr 2)
        return ch(v and 31) or (ch((v shr 5) and 31) shl 8) or (ch((v shr 10) and 31) shl 16) or (0xFF shl 24)
    }

    private val black = 0xFF shl 24

    @Test
    fun doubleSizeGivesEachGbaPixelA2x2Block() {
        val out = NativeBridge.debugDrawFrame(frame, 480, 320)
        assertNotNull("offscreen OpenGL ES 3 drawing failed", out)
        for (y in 0 until 320) for (x in 0 until 480) {
            assertEquals("pixel ($x, $y)", rgba(frame[(y / 2) * 240 + x / 2]), out!![y * 480 + x])
        }
    }

    @Test
    fun wideSurfaceGetsBlackBarsAndACentredPicture() {
        val out = NativeBridge.debugDrawFrame(frame, 720, 320)
        assertNotNull("offscreen OpenGL ES 3 drawing failed", out)
        for (y in 0 until 320) for (x in 0 until 720) {
            val want = if (x < 120 || x >= 600) black else rgba(frame[(y / 2) * 240 + (x - 120) / 2])
            assertEquals("pixel ($x, $y)", want, out!![y * 720 + x])
        }
    }

    @Test
    fun theBarsAreTheSkinsBodyColour() { // 8b: Ice's body A9D3EA round the picture
        val ice = Skins.byId("ice").body
        val out = NativeBridge.debugDrawFrameOn(frame, 720, 320, Settings.SIZE_FIT, false, ice)
        assertNotNull("offscreen OpenGL ES 3 drawing failed", out)
        val iceRgba = 0xA9 or (0xD3 shl 8) or (0xEA shl 16) or (0xFF shl 24)
        for (y in 0 until 320) for (x in 0 until 720) {
            val want = if (x < 120 || x >= 600) iceRgba else rgba(frame[(y / 2) * 240 + (x - 120) / 2])
            assertEquals("pixel ($x, $y)", want, out!![y * 720 + x])
        }
    }
}
```

- [x] **Step 2: Run it to see it fail**

```bash
bash tools/dev.sh androidtest 2>&1 | grep -E "^e: " | head -2
```

Expected: FAIL to compile — `Unresolved reference 'debugDrawFrameOn'`.

- [x] **Step 3: Write the code**

Replace `platform/android/video_gl.h`:

```c
#ifndef GBAEMU_VIDEO_GL_H
#define GBAEMU_VIDEO_GL_H

#include <GLES3/gl3.h>
#include <stdbool.h>
#include <stdint.h>

/* Draws 240x160 BGR555 frames with OpenGL ES 3.0: one integer texture, one quad, a shader that
 * unpacks the 15-bit colour (sharp: the nearest texel; smooth: the 4 nearest mixed). Every call needs the
 * same current GLES 3 context. */
typedef struct {
    GLuint program, texture, vao;
    GLint smooth_loc;
} video_gl_t;

bool video_gl_init(video_gl_t *v);                          /* false if the shader will not build */
void video_gl_upload(video_gl_t *v, const uint16_t *pixels); /* a new frame */
/* The surface in background (0xAARRGGBB, the skin's body colour; black before 8b) + the frame, placed by
 * fit_rect_mode(fit_mode: fit_mode_t), sharp or smooth. */
void video_gl_draw(video_gl_t *v, int width, int height, int fit_mode, bool smooth, uint32_t background);
void video_gl_destroy(video_gl_t *v);

#endif
```

Replace `platform/android/video_gl.c`:

```c
#include "video_gl.h"

#include <android/log.h>

#include "fit_rect.h"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)

/* A quad from gl_VertexID (triangle strip, no vertex buffer); uv (0,0) is the bottom left. */
static const char *VERTEX_SRC =
    "#version 300 es\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* Row 0 of the frame at the top; each 5-bit channel c becomes (c << 3) | (c >> 2). Sharp: the nearest texel.
 * Smooth: the 4 texels around the point mixed by distance (bilinear; an integer texture can't filter itself). */
static const char *FRAGMENT_SRC =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp usampler2D;\n"
    "uniform usampler2D u_frame;\n"
    "uniform int u_smooth;\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "vec3 texel(ivec2 t) {\n"
    "    uint p = texelFetch(u_frame, clamp(t, ivec2(0), ivec2(239, 159)), 0).r;\n"
    "    uvec3 c = uvec3(p & 31u, (p >> 5) & 31u, (p >> 10) & 31u);\n"
    "    return vec3((c << 3) | (c >> 2)) / 255.0;\n"
    "}\n"
    "void main() {\n"
    "    vec2 s = vec2(v_uv.x * 240.0, (1.0 - v_uv.y) * 160.0);\n"
    "    if (u_smooth == 0) {\n"
    "        o_color = vec4(texel(ivec2(min(s.x, 239.0), min(s.y, 159.0))), 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec2 p = s - 0.5;\n"
    "    ivec2 i = ivec2(floor(p));\n"
    "    vec2 f = p - floor(p);\n"
    "    vec3 top = mix(texel(i), texel(i + ivec2(1, 0)), f.x);\n"
    "    vec3 bottom = mix(texel(i + ivec2(0, 1)), texel(i + ivec2(1, 1)), f.x);\n"
    "    o_color = vec4(mix(top, bottom, f.y), 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        LOGE("shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool video_gl_init(video_gl_t *v) {
    GLuint vs = compile(GL_VERTEX_SHADER, VERTEX_SRC), fs = compile(GL_FRAGMENT_SHADER, FRAGMENT_SRC);
    if (!vs || !fs) return false;
    v->program = glCreateProgram();
    glAttachShader(v->program, vs);
    glAttachShader(v->program, fs);
    glLinkProgram(v->program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(v->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("shader program did not link");
        return false;
    }
    glUseProgram(v->program);
    glUniform1i(glGetUniformLocation(v->program, "u_frame"), 0);
    v->smooth_loc = glGetUniformLocation(v->program, "u_smooth");

    glGenTextures(1, &v->texture);
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R16UI, 240, 160);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); /* integer textures need NEAREST */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenVertexArrays(1, &v->vao);
    return glGetError() == GL_NO_ERROR;
}

void video_gl_upload(video_gl_t *v, const uint16_t *pixels) {
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 240, 160, GL_RED_INTEGER, GL_UNSIGNED_SHORT, pixels);
}

void video_gl_draw(video_gl_t *v, int width, int height, int fit_mode, bool smooth, uint32_t background) {
    glViewport(0, 0, width, height);
    glClearColor((float)((background >> 16) & 0xFF) / 255.0f, (float)((background >> 8) & 0xFF) / 255.0f,
                 (float)(background & 0xFF) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    fit_rect_t r = fit_rect_mode(width, height, (fit_mode_t)fit_mode);
    if (r.w == 0) return;
    glViewport(r.x, r.y, r.w, r.h);
    glUseProgram(v->program);
    glUniform1i(v->smooth_loc, smooth ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glBindVertexArray(v->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void video_gl_destroy(video_gl_t *v) {
    if (v->vao) glDeleteVertexArrays(1, &v->vao);
    if (v->texture) glDeleteTextures(1, &v->texture);
    if (v->program) glDeleteProgram(v->program);
    v->vao = v->texture = v->program = 0;
}
```

Replace `platform/android/render_thread.h`:

```c
#ifndef GBAEMU_RENDER_THREAD_H
#define GBAEMU_RENDER_THREAD_H

#include <android/native_window.h>
#include <stdbool.h>
#include <stdint.h>

#include "triple_buffer.h"

/* The thread that owns OpenGL: once per display refresh (AChoreographer) it draws the newest
 * frame from the triple buffer into the current window. */
typedef struct render_thread render_thread_t;

render_thread_t *render_create(triple_buffer_t *tb);
/* The window appeared, changed (w) or went away (NULL). Returns only after the render thread has
 * switched over (and let go of the old window); false if OpenGL ES 3 could not draw into w. */
bool render_set_window(render_thread_t *r, ANativeWindow *w);
/* The picture settings (fit_mode: fit_mode_t; smooth pixels): used from the next frame on, from any thread. */
void render_set_picture(render_thread_t *r, int fit_mode, bool smooth);
/* The colour round the picture (0xAARRGGBB, the skin's body): from the next frame on, from any thread. */
void render_set_background(render_thread_t *r, uint32_t argb);
void render_destroy(render_thread_t *r);

#endif
```

Replace `platform/android/render_thread.c`:

```c
#include "render_thread.h"

#include <android/choreographer.h>
#include <android/log.h>
#include <android/looper.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#include "egl_ctx.h"
#include "video_gl.h"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)

struct render_thread {
    pthread_t thread;
    triple_buffer_t *tb;
    int pipe_fd[2]; /* messages to the thread: 'W' window changed, 'Q' quit */

    pthread_mutex_t lock; /* guards pending/requested/handled/last_ok */
    pthread_cond_t done;
    ANativeWindow *pending;
    atomic_int fit_mode; /* the picture settings, set from any thread */
    atomic_bool smooth;
    atomic_uint background; /* 0xAARRGGBB round the picture */
    unsigned requested, handled;
    bool last_ok;

    /* Only the render thread touches these. */
    AChoreographer *choreographer;
    egl_ctx_t egl;
    bool egl_ready, gl_ready, frame_posted, quit;
    EGLSurface surface;
    ANativeWindow *window;
    video_gl_t gl;
};

static void post_frame(render_thread_t *r);

static void drop_surface(render_thread_t *r) {
    if (r->surface != EGL_NO_SURFACE) {
        eglMakeCurrent(r->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(r->egl.display, r->surface);
        r->surface = EGL_NO_SURFACE;
    }
    if (r->window) ANativeWindow_release(r->window);
    r->window = NULL;
}

/* Tear down the whole context (it was lost, or the thread is ending); GL objects go with it. */
static void drop_context(render_thread_t *r) {
    if (r->surface != EGL_NO_SURFACE) {
        eglMakeCurrent(r->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(r->egl.display, r->surface);
        r->surface = EGL_NO_SURFACE;
    }
    if (r->egl_ready) egl_ctx_destroy(&r->egl);
    r->egl_ready = r->gl_ready = false;
}

/* Surface for r->window; context and GL objects made on first use. */
static bool make_surface(render_thread_t *r) {
    if (!r->egl_ready) {
        r->egl_ready = egl_ctx_init(&r->egl, false);
        if (!r->egl_ready) return false;
    }
    r->surface = eglCreateWindowSurface(r->egl.display, r->egl.config, r->window, NULL);
    if (r->surface == EGL_NO_SURFACE || !eglMakeCurrent(r->egl.display, r->surface, r->surface, r->egl.context)) {
        LOGE("window surface failed: 0x%x", eglGetError());
        return false;
    }
    if (!r->gl_ready) {
        r->gl_ready = video_gl_init(&r->gl);
        if (!r->gl_ready) return false;
        bool fresh;
        video_gl_upload(&r->gl, tb_take(r->tb, &fresh)); /* show the newest frame straight away */
    }
    post_frame(r);
    return true;
}

static bool apply_window(render_thread_t *r, ANativeWindow *w) {
    if (w && w == r->window) { /* same window, new size: drawing reads the size every frame */
        ANativeWindow_release(w);
        return r->gl_ready && r->surface != EGL_NO_SURFACE;
    }
    drop_surface(r);
    if (!w) return true;
    r->window = w;
    return make_surface(r);
}

static void on_frame(render_thread_t *r) {
    r->frame_posted = false;
    if (r->quit || r->surface == EGL_NO_SURFACE || !r->gl_ready) return;
    bool fresh;
    const uint16_t *frame = tb_take(r->tb, &fresh);
    if (fresh) video_gl_upload(&r->gl, frame);
    EGLint w = 0, h = 0;
    eglQuerySurface(r->egl.display, r->surface, EGL_WIDTH, &w);
    eglQuerySurface(r->egl.display, r->surface, EGL_HEIGHT, &h);
    video_gl_draw(&r->gl, w, h, atomic_load(&r->fit_mode), atomic_load(&r->smooth), atomic_load(&r->background));
    if (!eglSwapBuffers(r->egl.display, r->surface) && eglGetError() == EGL_CONTEXT_LOST) {
        LOGE("EGL context lost: rebuilding");
        drop_context(r);
        if (!make_surface(r)) return;
    }
    post_frame(r);
}

static void frame_cb64(int64_t frame_time_ns, void *data) {
    (void)frame_time_ns;
    on_frame(data);
}

static void frame_cb(long frame_time_ns, void *data) {
    (void)frame_time_ns;
    on_frame(data);
}

static void post_frame(render_thread_t *r) {
    if (r->frame_posted) return;
    r->frame_posted = true;
    if (__builtin_available(android 29, *)) {
        AChoreographer_postFrameCallback64(r->choreographer, frame_cb64, r);
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        AChoreographer_postFrameCallback(r->choreographer, frame_cb, r);
#pragma clang diagnostic pop
    }
}

static int on_message(int fd, int events, void *data) {
    (void)events;
    render_thread_t *r = data;
    char msg;
    if (read(fd, &msg, 1) != 1) return 1;
    if (msg == 'Q') {
        r->quit = true;
        return 1;
    }
    pthread_mutex_lock(&r->lock);
    ANativeWindow *w = r->pending;
    r->pending = NULL;
    unsigned seq = r->requested;
    pthread_mutex_unlock(&r->lock);
    bool ok = apply_window(r, w);
    pthread_mutex_lock(&r->lock);
    r->handled = seq;
    r->last_ok = ok;
    pthread_cond_broadcast(&r->done);
    pthread_mutex_unlock(&r->lock);
    return 1;
}

static void *render_main(void *arg) {
    render_thread_t *r = arg;
    ALooper *looper = ALooper_prepare(0);
    ALooper_addFd(looper, r->pipe_fd[0], 1, ALOOPER_EVENT_INPUT, on_message, r);
    r->choreographer = AChoreographer_getInstance();
    while (!r->quit) ALooper_pollOnce(-1, NULL, NULL, NULL);
    ALooper_removeFd(looper, r->pipe_fd[0]);
    drop_surface(r);
    drop_context(r);
    return NULL;
}

render_thread_t *render_create(triple_buffer_t *tb) {
    render_thread_t *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->tb = tb;
    r->surface = EGL_NO_SURFACE;
    atomic_init(&r->background, 0xFF000000u); /* black until the skin's colour comes */
    if (pipe(r->pipe_fd) != 0) {
        free(r);
        return NULL;
    }
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->done, NULL);
    pthread_create(&r->thread, NULL, render_main, r);
    return r;
}

bool render_set_window(render_thread_t *r, ANativeWindow *w) {
    if (w) ANativeWindow_acquire(w); /* the render thread owns this reference now */
    pthread_mutex_lock(&r->lock);
    r->pending = w;
    unsigned seq = ++r->requested;
    pthread_mutex_unlock(&r->lock);
    char msg = 'W';
    write(r->pipe_fd[1], &msg, 1);
    pthread_mutex_lock(&r->lock);
    while (r->handled != seq) pthread_cond_wait(&r->done, &r->lock);
    bool ok = r->last_ok;
    pthread_mutex_unlock(&r->lock);
    return ok;
}

void render_set_picture(render_thread_t *r, int fit_mode, bool smooth) {
    atomic_store(&r->fit_mode, fit_mode);
    atomic_store(&r->smooth, smooth);
}

void render_set_background(render_thread_t *r, uint32_t argb) {
    atomic_store(&r->background, argb);
}

void render_destroy(render_thread_t *r) {
    if (!r) return;
    char msg = 'Q';
    write(r->pipe_fd[1], &msg, 1);
    pthread_join(r->thread, NULL);
    close(r->pipe_fd[0]);
    close(r->pipe_fd[1]);
    pthread_mutex_destroy(&r->lock);
    pthread_cond_destroy(&r->done);
    free(r);
}
```

Replace `platform/android/gl_readback.h`:

```c
#ifndef GBAEMU_GL_READBACK_H
#define GBAEMU_GL_READBACK_H

#include <stdbool.h>
#include <stdint.h>

/* Test helper: draws one 240x160 BGR555 frame with the real video_gl code (fit_mode, smooth, background: as video_gl_draw) into an offscreen
 * width x height surface on the calling thread and reads it back, top row first, one
 * little-endian RGBA pixel per uint32_t (r | g << 8 | b << 16 | a << 24). */
bool gl_readback(const uint16_t *pixels, int width, int height, int fit_mode, bool smooth, uint32_t background,
                 uint32_t *out);

#endif
```

Replace `platform/android/gl_readback.c`:

```c
#include "gl_readback.h"

#include <string.h>

#include "egl_ctx.h"
#include "video_gl.h"

bool gl_readback(const uint16_t *pixels, int width, int height, int fit_mode, bool smooth, uint32_t background,
                 uint32_t *out) {
    egl_ctx_t e;
    if (!egl_ctx_init(&e, true)) return false;
    const EGLint pb_attrs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(e.display, e.config, pb_attrs);
    bool ok = s != EGL_NO_SURFACE && eglMakeCurrent(e.display, s, s, e.context);
    video_gl_t v = {0, 0, 0, 0};
    if (ok) ok = video_gl_init(&v);
    if (ok) {
        video_gl_upload(&v, pixels);
        video_gl_draw(&v, width, height, fit_mode, smooth, background);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out);
        ok = glGetError() == GL_NO_ERROR;
        for (int y = 0; y < height / 2; y++) { /* GL rows start at the bottom */
            uint32_t *a = out + (size_t)y * width, *b = out + (size_t)(height - 1 - y) * width;
            for (int x = 0; x < width; x++) {
                uint32_t t = a[x];
                a[x] = b[x];
                b[x] = t;
            }
        }
        video_gl_destroy(&v);
    }
    if (s != EGL_NO_SURFACE) {
        eglMakeCurrent(e.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(e.display, s);
    }
    egl_ctx_destroy(&e);
    return ok;
}
```

Replace `platform/android/jni_bridge.c`:

```c
/* JNI entry points for com.tengen.gbaemu.NativeBridge. Called from one thread at a time (the UI
 * thread in the app, the test thread in on-device tests); they keep one game session. */
#include <android/native_window_jni.h>
#include <inttypes.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>

#include "emu_thread.h"
#include "frame_hash.h"
#include "gl_readback.h"
#include "render_thread.h"

static emu_t *s_emu;
static render_thread_t *s_render;
static ANativeWindow *s_window; /* a window that arrived before start() */

/* Settings kept for every game (set any time; the BIOS and the rewind budget are used by the next loadRom). */
static uint8_t *s_bios;       /* a BIOS file's 16 KiB, or NULL: the built-in BIOS */
static size_t s_rewind_budget = EMU_REWIND_DEFAULT;
static bool s_sound = true;
static int s_sound_bursts = 2;
static int s_fit_mode;        /* fit_mode_t */
static bool s_smooth;
static uint32_t s_background = 0xFF000000u; /* round the picture: the skin's body (8b) */
/* The next game's own settings (used by the next loadRom only). */
static int s_next_save_type = EMU_AUTO, s_next_clock = EMU_AUTO;

#define JNI_FN(name) Java_com_tengen_gbaemu_NativeBridge_##name

static void stop_session(void) {
    render_destroy(s_render); /* first: it reads the emulator's frames */
    s_render = NULL;
    emu_destroy(s_emu);
    s_emu = NULL;
}

JNIEXPORT jint JNICALL JNI_FN(loadRom)(JNIEnv *env, jobject self, jobject rom, jstring save_path) {
    (void)self;
    stop_session(); /* writes the previous game's save */
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    if (!data || size <= 0) return GBA_ERR_ROM_SIZE;
    const char *path = save_path ? (*env)->GetStringUTFChars(env, save_path, NULL) : NULL;
    emu_options_t opt = emu_options_default();
    opt.bios = s_bios;
    opt.bios_size = s_bios ? 16384 : 0;
    opt.save_type = s_next_save_type;
    opt.clock = s_next_clock;
    opt.rewind_budget = s_rewind_budget;
    opt.sound = s_sound;
    opt.sound_bursts = s_sound_bursts;
    s_next_save_type = s_next_clock = EMU_AUTO;
    gba_err_t err;
    s_emu = emu_create(data, (size_t)size, path, &opt, &err);
    if (path) (*env)->ReleaseStringUTFChars(env, save_path, path);
    return err;
}

/* The BIOS for the next games: 16 KiB, or null for the built-in one. GBA_ERR_BIOS_SIZE (nothing changes) otherwise. */
JNIEXPORT jint JNICALL JNI_FN(setBios)(JNIEnv *env, jobject self, jbyteArray bytes) {
    (void)self;
    if (!bytes) {
        free(s_bios);
        s_bios = NULL;
        return GBA_OK;
    }
    if ((*env)->GetArrayLength(env, bytes) != 16384) return GBA_ERR_BIOS_SIZE;
    uint8_t *copy = malloc(16384);
    if (!copy) return GBA_ERR_NOMEM;
    (*env)->GetByteArrayRegion(env, bytes, 0, 16384, (jbyte *)copy);
    free(s_bios);
    s_bios = copy;
    return GBA_OK;
}

/* Bytes for the next games' rewind snapshots (0 = rewind off). */
JNIEXPORT void JNICALL JNI_FN(setRewindBudget)(JNIEnv *env, jobject self, jlong bytes) {
    (void)env;
    (void)self;
    s_rewind_budget = bytes > 0 ? (size_t)bytes : 0;
}

/* The next loadRom's save type (a gba_save_type_t, or -1 = detect) and clock (-1 = detect, 0 = none, 1 = one). */
JNIEXPORT void JNICALL JNI_FN(setGameOverrides)(JNIEnv *env, jobject self, jint save_type, jint clock) {
    (void)env;
    (void)self;
    s_next_save_type = save_type;
    s_next_clock = clock;
}

/* Sound on/off and AAudio's buffer in bursts (2, 4, 8): at once for a running game, and for the next ones. */
JNIEXPORT void JNICALL JNI_FN(setSound)(JNIEnv *env, jobject self, jboolean on, jint bursts) {
    (void)env;
    (void)self;
    s_sound = on == JNI_TRUE;
    s_sound_bursts = bursts;
    if (s_emu) emu_set_sound(s_emu, s_sound, bursts);
}

/* How the picture fills the screen (fit_mode_t) and whether pixels are smoothed: from the next frame on. */
JNIEXPORT void JNICALL JNI_FN(setPicture)(JNIEnv *env, jobject self, jint fit_mode, jboolean smooth) {
    (void)env;
    (void)self;
    s_fit_mode = fit_mode;
    s_smooth = smooth == JNI_TRUE;
    if (s_render) render_set_picture(s_render, s_fit_mode, s_smooth);
}

/* The colour round the picture (0xAARRGGBB: the skin's body): from the next frame on. */
JNIEXPORT void JNICALL JNI_FN(setBackground)(JNIEnv *env, jobject self, jint argb) {
    (void)env;
    (void)self;
    s_background = (uint32_t)argb;
    if (s_render) render_set_background(s_render, s_background);
}

JNIEXPORT jstring JNICALL JNI_FN(romTitle)(JNIEnv *env, jobject self) {
    (void)self;
    return (*env)->NewStringUTF(env, s_emu ? emu_rom_title(s_emu) : "");
}

JNIEXPORT jint JNICALL JNI_FN(saveStatus)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_save_status(s_emu) : EMU_SAVE_NONE;
}

JNIEXPORT jint JNICALL JNI_FN(importSave)(JNIEnv *env, jobject self, jbyteArray bytes) {
    (void)self;
    if (!s_emu) return GBA_ERR_SAVE_SIZE;
    jsize n = (*env)->GetArrayLength(env, bytes);
    jbyte *b = (*env)->GetByteArrayElements(env, bytes, NULL);
    gba_err_t err = emu_import_save(s_emu, (const uint8_t *)b, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, bytes, b, JNI_ABORT);
    return err;
}

JNIEXPORT jboolean JNICALL JNI_FN(saveState)(JNIEnv *env, jobject self, jstring path) {
    (void)self;
    if (!s_emu) return JNI_FALSE;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    bool ok = emu_state_save(s_emu, p);
    (*env)->ReleaseStringUTFChars(env, path, p);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL JNI_FN(loadState)(JNIEnv *env, jobject self, jstring path) {
    (void)self;
    if (!s_emu) return GBA_ERR_STATE;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    gba_err_t err = emu_state_load(s_emu, p);
    (*env)->ReleaseStringUTFChars(env, path, p);
    return err;
}

/* [the type it reads as, the first bad line (0 = none)]; no game needed. */
JNIEXPORT jintArray JNICALL JNI_FN(checkCheat)(JNIEnv *env, jobject self, jstring text, jint type) {
    (void)self;
    const char *t = (*env)->GetStringUTFChars(env, text, NULL);
    int bad = 1;
    gba_cheat_type_t used = t ? gba_cheat_check(t, (gba_cheat_type_t)type, &bad) : (gba_cheat_type_t)type;
    if (t) (*env)->ReleaseStringUTFChars(env, text, t);
    jint v[2] = {(jint)used, bad};
    jintArray out = (*env)->NewIntArray(env, 2);
    if (out) (*env)->SetIntArrayRegion(env, out, 0, 2, v);
    return out;
}

/* The switched-on cheats (only while paused or before start): 0, or a gba_err_t. */
JNIEXPORT jint JNICALL JNI_FN(setCheats)(JNIEnv *env, jobject self, jobjectArray texts, jintArray types) {
    (void)self;
    if (!s_emu) return GBA_ERR_CHEAT;
    jsize n = (*env)->GetArrayLength(env, texts);
    if ((*env)->GetArrayLength(env, types) != n) return GBA_ERR_CHEAT;
    const char **t = calloc((size_t)n + 1, sizeof *t);
    jstring *js = calloc((size_t)n + 1, sizeof *js);
    gba_cheat_type_t *ty = calloc((size_t)n + 1, sizeof *ty);
    jint *raw = (*env)->GetIntArrayElements(env, types, NULL);
    gba_err_t err = t && js && ty && raw ? GBA_OK : GBA_ERR_NOMEM;
    for (jsize i = 0; err == GBA_OK && i < n; i++) {
        js[i] = (jstring)(*env)->GetObjectArrayElement(env, texts, i);
        t[i] = js[i] ? (*env)->GetStringUTFChars(env, js[i], NULL) : NULL;
        if (!t[i]) err = GBA_ERR_CHEAT;
        ty[i] = (gba_cheat_type_t)raw[i];
    }
    if (err == GBA_OK) err = emu_cheats_set(s_emu, t, ty, (size_t)n);
    for (jsize i = 0; js && i < n; i++) {
        if (js[i] && t && t[i]) (*env)->ReleaseStringUTFChars(env, js[i], t[i]);
        if (js[i]) (*env)->DeleteLocalRef(env, js[i]);
    }
    if (raw) (*env)->ReleaseIntArrayElements(env, types, raw, JNI_ABORT);
    free(t);
    free(js);
    free(ty);
    return (jint)err;
}

JNIEXPORT jshortArray JNICALL JNI_FN(frameBgr555)(JNIEnv *env, jobject self) {
    (void)self;
    static uint16_t px[240 * 160];
    if (!s_emu || !emu_frame(s_emu, px)) return NULL;
    jshortArray out = (*env)->NewShortArray(env, 240 * 160);
    (*env)->SetShortArrayRegion(env, out, 0, 240 * 160, (const jshort *)px);
    return out;
}

JNIEXPORT void JNICALL JNI_FN(setMotion)(JNIEnv *env, jobject self, jint tilt_x, jint tilt_y, jint turn) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_motion(s_emu, tilt_x, tilt_y, turn);
}

JNIEXPORT void JNICALL JNI_FN(setLight)(JNIEnv *env, jobject self, jint level) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_light(s_emu, level);
}

JNIEXPORT jint JNICALL JNI_FN(cartHardware)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_cart_hardware(s_emu) : 0;
}

JNIEXPORT jboolean JNICALL JNI_FN(rumbleActive)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu && emu_rumble_active(s_emu) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL JNI_FN(saveError)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu && emu_save_error(s_emu) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(start)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (!s_emu) return;
    if (!s_render) {
        s_render = render_create(emu_frames(s_emu));
        if (s_render) {
            render_set_picture(s_render, s_fit_mode, s_smooth);
            render_set_background(s_render, s_background);
        }
    }
    if (s_render && s_window) {
        render_set_window(s_render, s_window);
        ANativeWindow_release(s_window);
        s_window = NULL;
    }
    emu_start(s_emu);
}

JNIEXPORT jboolean JNICALL JNI_FN(setSurface)(JNIEnv *env, jobject self, jobject surface) {
    (void)self;
    ANativeWindow *w = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (!s_render) { /* not started yet: hand it over in start() */
        if (s_window) ANativeWindow_release(s_window);
        s_window = w;
        return JNI_TRUE;
    }
    bool ok = render_set_window(s_render, w);
    if (w) ANativeWindow_release(w);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(stop)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    stop_session();
    if (s_window) ANativeWindow_release(s_window);
    s_window = NULL;
}

JNIEXPORT void JNICALL JNI_FN(pause)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_pause(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(resume)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_resume(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(setKeys)(JNIEnv *env, jobject self, jint pressed) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_keys(s_emu, (uint16_t)(pressed & 0x3FF));
}

JNIEXPORT void JNICALL JNI_FN(setFastForward)(JNIEnv *env, jobject self, jint speed) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_fast_forward(s_emu, speed);
}

JNIEXPORT void JNICALL JNI_FN(setRewinding)(JNIEnv *env, jobject self, jboolean on) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_rewinding(s_emu, on);
}

JNIEXPORT jlongArray JNICALL JNI_FN(debugRewindStats)(JNIEnv *env, jobject self) {
    (void)self;
    int64_t v[3] = {0, 0, 0};
    if (s_emu) emu_rewind_stats(s_emu, v);
    jlongArray out = (*env)->NewLongArray(env, 3);
    if (out) (*env)->SetLongArrayRegion(env, out, 0, 3, (const jlong *)v);
    return out;
}

JNIEXPORT jint JNICALL JNI_FN(debugPeek32)(JNIEnv *env, jobject self, jint addr) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_debug_peek32(s_emu, (uint32_t)addr) : 0;
}

JNIEXPORT jlong JNICALL JNI_FN(debugFrameCount)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jlong)emu_frame_count(s_emu) : 0;
}

JNIEXPORT jlongArray JNICALL JNI_FN(debugAudioStats)(JNIEnv *env, jobject self) {
    (void)self;
    int64_t v[7] = {0, 0, 0, 0, 0, 0, 0};
    if (s_emu) emu_audio_stats(s_emu, v);
    jlongArray out = (*env)->NewLongArray(env, 7);
    (*env)->SetLongArrayRegion(env, out, 0, 7, (const jlong *)v);
    return out;
}

JNIEXPORT void JNICALL JNI_FN(debugSetAudioEnabled)(JNIEnv *env, jobject self, jboolean enabled) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_audio_enabled(s_emu, enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL JNI_FN(debugSimulateAudioDisconnect)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_simulate_audio_disconnect(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(debugSimulateStuckStream)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_simulate_stuck_stream(s_emu);
}

JNIEXPORT jbyteArray JNICALL JNI_FN(debugSaveRam)(JNIEnv *env, jobject self) {
    (void)self;
    if (!s_emu) return NULL;
    const uint8_t *data;
    size_t n = emu_debug_saveram(s_emu, &data);
    jbyteArray out = (*env)->NewByteArray(env, (jsize)n);
    (*env)->SetByteArrayRegion(env, out, 0, (jsize)n, (const jbyte *)data);
    return out;
}

JNIEXPORT jintArray JNICALL JNI_FN(debugMotionSeen)(JNIEnv *env, jobject self) {
    (void)self;
    int32_t v[4] = {0, 0, 0, 0};
    if (s_emu) emu_motion_seen(s_emu, v);
    jintArray out = (*env)->NewIntArray(env, 4);
    (*env)->SetIntArrayRegion(env, out, 0, 4, (const jint *)v);
    return out;
}

JNIEXPORT jint JNICALL JNI_FN(debugKeysSeen)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_keys_seen(s_emu) : 0;
}

#define REWIND_EVERY 4

/* Headless run on the calling thread, exactly like `testrunner --frames n [--state-at k]
 * [--rewind-at f:s]` (state_at / rewind_at < 0: none): "<frame hash> <audio hash>", or NULL when the ROM
 * or the state did not load, or a rewind step found no snapshot. */
static jstring run_headless(JNIEnv *env, jobject rom, jint frames, jint state_at, jint rewind_at, jint rewind_steps) {
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    gba_t *g = gba_create(NULL);
    if (!g || !data || gba_load_rom(g, data, (size_t)size) != GBA_OK) {
        gba_destroy(g);
        return NULL;
    }
    uint64_t audio_hash = AUDIO_HASH_INIT;
    int16_t buf[1024 * 2];
    uint64_t *hash_before = NULL; /* the audio hash before each frame's sound was read */
    if (rewind_at >= 0) {
        hash_before = malloc(sizeof(uint64_t) * (size_t)frames);
        if (!hash_before || gba_rewind_configure(g, 64u * 1024 * 1024, REWIND_EVERY) != GBA_OK) {
            free(hash_before);
            gba_destroy(g);
            return NULL;
        }
    }
    bool rewound = false;
    for (jint f = 0; f < frames; f++) {
        if (f == rewind_at && !rewound) { /* as testrunner --rewind-at */
            for (jint s = 0; s < rewind_steps; s++) {
                if (!gba_rewind_step_back(g)) {
                    free(hash_before);
                    gba_destroy(g);
                    return NULL;
                }
            }
            jint to = rewind_at - rewind_at % REWIND_EVERY - REWIND_EVERY * (rewind_steps - (rewind_at % REWIND_EVERY ? 1 : 0));
            f = to;
            audio_hash = hash_before[to - 1];
            rewound = true;
        }
        if (hash_before) hash_before[f] = audio_hash;
        if (f == state_at) { /* carry on in a brand-new machine loaded from a state */
            size_t n = gba_state_size(g);
            uint8_t *state = malloc(n);
            gba_t *fresh = gba_create(NULL);
            bool ok = state && fresh && gba_state_save(g, state, n) == n &&
                      gba_load_rom(fresh, data, (size_t)size) == GBA_OK && gba_state_load(fresh, state, n) == GBA_OK;
            free(state);
            gba_destroy(g);
            g = fresh;
            if (!ok) {
                gba_destroy(g);
                return NULL;
            }
        }
        gba_set_keys(g, 0);
        gba_run_frame(g);
        size_t n;
        while ((n = gba_audio_read(g, buf, 1024)) > 0) audio_hash = audio_hash_update(audio_hash, buf, n * 2);
    }
    free(hash_before);
    char out[40];
    snprintf(out, sizeof out, "%016" PRIx64 " %016" PRIx64, frame_hash(gba_framebuffer(g)), audio_hash);
    gba_destroy(g);
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT jstring JNICALL JNI_FN(debugRunFrames)(JNIEnv *env, jobject self, jobject rom, jint frames) {
    (void)self;
    return run_headless(env, rom, frames, -1, -1, 0);
}

JNIEXPORT jstring JNICALL JNI_FN(debugRunFramesViaState)(JNIEnv *env, jobject self, jobject rom, jint frames,
                                                         jint state_at) {
    (void)self;
    return run_headless(env, rom, frames, state_at, -1, 0);
}

JNIEXPORT jstring JNICALL JNI_FN(debugRunFramesViaRewind)(JNIEnv *env, jobject self, jobject rom, jint frames,
                                                          jint rewind_at, jint rewind_steps) {
    (void)self;
    return run_headless(env, rom, frames, -1, rewind_at, rewind_steps);
}

/* Draws one frame offscreen with the real OpenGL code, round it the colour background (0xAARRGGBB); RGBA pixels top
 * row first, or null. */
JNIEXPORT jintArray JNICALL JNI_FN(debugDrawFrameOn)(JNIEnv *env, jobject self, jshortArray pixels, jint width,
                                                     jint height, jint fit_mode, jboolean smooth, jint background) {
    (void)self;
    if ((*env)->GetArrayLength(env, pixels) != 240 * 160 || width <= 0 || height <= 0) return NULL;
    jshort *px = (*env)->GetShortArrayElements(env, pixels, NULL);
    uint32_t *out = malloc((size_t)width * (size_t)height * 4);
    bool ok = out && gl_readback((const uint16_t *)px, width, height, fit_mode, smooth == JNI_TRUE, (uint32_t)background, out);
    (*env)->ReleaseShortArrayElements(env, pixels, px, JNI_ABORT);
    jintArray result = NULL;
    if (ok) {
        result = (*env)->NewIntArray(env, width * height);
        (*env)->SetIntArrayRegion(env, result, 0, width * height, (const jint *)out);
    }
    free(out);
    return result;
}

/* As debugDrawFrameOn on black. */
JNIEXPORT jintArray JNICALL JNI_FN(debugDrawFrameWith)(JNIEnv *env, jobject self, jshortArray pixels, jint width,
                                                       jint height, jint fit_mode, jboolean smooth) {
    return JNI_FN(debugDrawFrameOn)(env, self, pixels, width, height, fit_mode, smooth, (jint)0xFF000000u);
}

/* As debugDrawFrameWith with the picture as in 4a (fit, sharp). */
JNIEXPORT jintArray JNICALL JNI_FN(debugDrawFrame)(JNIEnv *env, jobject self, jshortArray pixels, jint width,
                                                   jint height) {
    return JNI_FN(debugDrawFrameWith)(env, self, pixels, width, height, 0, JNI_FALSE);
}

/* The loaded game's save type (gba_save_type_t) and extras (GBA_HW_*), detected or forced; -1 without a game. */
JNIEXPORT jintArray JNICALL JNI_FN(cartSetup)(JNIEnv *env, jobject self) {
    (void)self;
    jint v[2] = {-1, -1};
    if (s_emu) {
        v[0] = (jint)emu_save_type(s_emu);
        v[1] = (jint)emu_cart_hardware(s_emu);
    }
    jintArray out = (*env)->NewIntArray(env, 2);
    (*env)->SetIntArrayRegion(env, out, 0, 2, v);
    return out;
}
```

Replace `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.Surface
import java.nio.ByteBuffer

/** The C side (platform/android/jni_bridge.c). One game session at a time; call from one thread. */
object NativeBridge {
    init {
        System.loadLibrary("gbaemu")
    }

    /**
     * Creates the emulator with this ROM (a direct buffer; the core copies it) and loads its save file
     * from savePath if there is one (null: no save file). The game's saves go to that file ~1 s after
     * the game saves, and on pause/stop. 0 = OK, else a gba_err_t. Stops (and saves) a previous game.
     */
    external fun loadRom(rom: ByteBuffer, savePath: String?): Int

    fun loadRom(rom: ByteBuffer): Int = loadRom(rom, null)

    /** The BIOS for the next games: a 16 KiB BIOS file's bytes, or null = the built-in one. 0, or ERR_BIOS_SIZE. */
    external fun setBios(bytes: ByteArray?): Int

    /** Bytes for the next games' rewind snapshots (0 = rewind off). */
    external fun setRewindBudget(bytes: Long)

    /** The next loadRom only: a forced save type (a gba_save_type_t) and clock (0 none, 1 one); -1 = detect. */
    external fun setGameOverrides(saveType: Int, clock: Int)

    /** Sound on/off and AAudio's buffer in bursts (2, 4, 8): at once, and for the next games. */
    external fun setSound(on: Boolean, bursts: Int)

    /** How the picture fills the screen (Settings.SIZE_*) and whether pixels are smoothed: at once. */
    external fun setPicture(fitMode: Int, smooth: Boolean)

    /** The colour round the game picture (ARGB: the skin's body, 8b), from the next frame on. */
    external fun setBackground(argb: Int)

    /** After loadRom: [save type (gba_save_type_t), HW_* extras], detected or forced; -1s without a game. */
    external fun cartSetup(): IntArray

    /** The loaded ROM's header title (may be empty). */
    external fun romTitle(): String

    /** After loadRom: one of the SAVE_* values below. */
    external fun saveStatus(): Int

    /** Before start(): puts a .sav file's bytes in and writes the save file. 0 = OK, else a gba_err_t (nothing changed). */
    external fun importSave(bytes: ByteArray): Int

    /** True once after a save-file write failed. */
    external fun saveError(): Boolean

    /** Save states — only while paused (after pause()) or before start(). Writes the whole machine to path. */
    external fun saveState(path: String): Boolean

    /** 0 = loaded; else a gba_err_t (GBA_ERR_STATE = 5: another game, another version, damaged, or not paused). */
    external fun loadState(path: String): Int

    /** The current picture, 240×160 BGR555 (only while paused), or null. */
    external fun frameBgr555(): ShortArray?

    const val ERR_BIOS_SIZE = 2
    const val ERR_STATE = 5
    const val ERR_CHEAT = 6

    /** Reads a cheat's text without a game: [the type it reads as (CHEAT_*), the first bad line (0 = none)]. */
    external fun checkCheat(text: String, type: Int): IntArray

    /** The switched-on cheats (only while paused or before start()): 0, or ERR_CHEAT (then none are on). */
    external fun setCheats(texts: Array<String>, types: IntArray): Int

    const val CHEAT_AUTO = 0
    const val CHEAT_ACTION_REPLAY = 1
    const val CHEAT_GAMESHARK = 2
    const val CHEAT_CODEBREAKER = 3
    const val CHEAT_RAW = 4

    /** Cartridge sensors, applied at the next frame: tilt x/y and turn ±32767 (turn > 0 = clockwise). */
    external fun setMotion(tiltX: Int, tiltY: Int, turn: Int)

    /** The solar sensor's light: 0 = dark .. 255 = full sun. */
    external fun setLight(level: Int)

    /** The loaded cartridge's extras: HW_* bits below. */
    external fun cartHardware(): Int

    /** True while the cartridge's rumble motor runs (as of the last frame). */
    external fun rumbleActive(): Boolean

    const val HW_RTC = 1
    const val HW_SOLAR = 2
    const val HW_GYRO = 4
    const val HW_RUMBLE = 8
    const val HW_TILT = 16

    const val SAVE_NONE = 0 // no save chip: nothing to offer
    const val SAVE_LOADED = 1 // the save file was loaded
    const val SAVE_EMPTY = 2 // a save chip and no save file yet: offer an import
    const val SAVE_MISFIT = 3 // the save file does not fit the chip: not loaded, left untouched

    /** Starts the emulation and render threads for the loaded ROM. */
    external fun start()

    /** Stops both threads and frees the emulator. */
    external fun stop()

    /** Returns once the game has stopped and its save is written (at most 1 s). */
    external fun pause()

    external fun resume()

    /** The game's window appeared/changed (or went away: null). False if OpenGL ES 3 cannot draw into it. */
    external fun setSurface(surface: Surface?): Boolean

    /** Held GBA buttons (KeyMap bits). */
    external fun setKeys(pressed: Int)

    /** Fast-forward: 0 = normal, 2/4/8 = that many times faster (sound sped up), FF_MAX = flat out, silent. */
    external fun setFastForward(speed: Int)

    /** While true the game runs backwards (one snapshot of 4 frames per 1/60 s, silent); false plays on from there. */
    external fun setRewinding(on: Boolean)

    const val FF_MAX = -1

    // ---- test-only ----

    /** A word of the game's memory (only while paused or before start). */
    external fun debugPeek32(addr: Int): Int

    /** Frames the emulation thread has run since loadRom. */
    external fun debugFrameCount(): Long

    /** Sound since loadRom: [active 1/0, sample rate, frames per burst, played, silence-filled, loud, reopens]. */
    external fun debugAudioStats(): LongArray

    /** Before start(): false = no sound stream, the clock paces the game. */
    external fun debugSetAudioEnabled(enabled: Boolean)

    /** As if the sound device went away (headphones unplugged): the stream must reopen. */
    external fun debugSimulateAudioDisconnect()

    /** The stream silently stops taking sound: the watchdog must reopen it (after its 2 s start-up grace). */
    external fun debugSimulateStuckStream()

    /** The core's save bytes (call while paused), or null without a game. */
    external fun debugSaveRam(): ByteArray?

    /** The sensor values last applied to the core: [tilt x, tilt y, turn, light]. */
    external fun debugMotionSeen(): IntArray

    /** Buttons the game was given in any frame since the last call (KeyMap bits). */
    external fun debugKeysSeen(): Int

    /** Runs a ROM headless for n frames on this thread: "<frame hash> <audio hash>" like the PC test runner. */
    external fun debugRunFrames(rom: ByteBuffer, frames: Int): String?

    /** The same, but at frame stateAt it carries on in a new machine loaded from a save state (null: failed). */
    external fun debugRunFramesViaState(rom: ByteBuffer, frames: Int, stateAt: Int): String?

    /** The same, but at frame rewindAt it steps back rewindSteps snapshots (4 frames apart) and plays on (null: failed). */
    external fun debugRunFramesViaRewind(rom: ByteBuffer, frames: Int, rewindAt: Int, rewindSteps: Int): String?

    /** Rewind since start: [snapshots kept, their packed bytes, steps back taken]. */
    external fun debugRewindStats(): LongArray

    /** Draws one 240x160 BGR555 frame offscreen; RGBA pixels (r | g<<8 | b<<16 | a<<24), top row first. */
    external fun debugDrawFrame(pixels: ShortArray, width: Int, height: Int): IntArray?

    /** As debugDrawFrame, with a picture size (Settings.SIZE_*) and sharp or smooth pixels. */
    external fun debugDrawFrameWith(pixels: ShortArray, width: Int, height: Int, fitMode: Int, smooth: Boolean): IntArray?

    /** As debugDrawFrameWith, round the picture the colour [background] (ARGB) instead of black. */
    external fun debugDrawFrameOn(pixels: ShortArray, width: Int, height: Int, fitMode: Int, smooth: Boolean,
                                  background: Int): IntArray?
}
```

- [x] **Step 4: Build with no C warnings, then the tests**

```bash
touch platform/android/*.c && bash tools/dev.sh apk 2>&1 | grep -c "warning:"
rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |BUILD|Finished"
grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: |BUILD"
```

Expected: `0` C warnings; `BUILD SUCCESSFUL`; on-device `tests="57" failures="0"`; Kotlin `94 0`.

- [x] **Step 5: Commit**

```bash
git add platform/android/video_gl.h platform/android/video_gl.c platform/android/render_thread.h platform/android/render_thread.c platform/android/gl_readback.h platform/android/gl_readback.c platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt app/src/androidTest/java/com/tengen/gbaemu/PictureTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-8b-console-skins.md
git commit -m "feat(android): the colour round the picture from the skin (setBackground, glClearColor)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 4: Which buttons are over the game itself

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/TouchLayout.kt` (`fitMode`; `game` = where OpenGL draws the game inside the picture's box — `gameIn`, a copy of `fit_rect_mode`; `overGame`, `dpadOverGame`; `with` keeps `fitMode`)
- Test: `app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt` (+2: `theGameInsideThePicturesBoxFollowsTheSizeMode`, `sidewaysOnlySelectAndStartAreOverTheGame`)

(Found in the prototype: sideways the picture's box is the whole screen, so every button counted as "over the picture" and was see-through — washed out on a coloured body. The skins use `overGame`; Plain keeps `overPicture`.)

- [x] **Step 1: Write the failing tests**

Replace `app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TouchLayoutTest {
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
```

- [x] **Step 2: Run them to see them fail**

```bash
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: " | head -2
```

Expected: FAIL to compile — `Unresolved reference 'game'`.

- [x] **Step 3: Write the code**

Replace `app/src/main/java/com/tengen/gbaemu/TouchLayout.kt`:

```kotlin
package com.tengen.gbaemu

import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.roundToInt

/** What the layout editor can move: the game picture, the D-pad (as one) and each button. */
enum class LayoutItem(val id: String, val label: String) {
    PICTURE("picture", "Game picture"), DPAD("dpad", "D-pad"), L("l", "L"), R("r", "R"), REWIND("rewind", "Rewind"),
    FAST("fast", "Fast-forward"), A("a", "A"), B("b", "B"), SELECT("select", "Select"), START("start", "Start");

    companion object {
        fun byId(id: String): LayoutItem? = entries.firstOrNull { it.id == id }
    }
}

/** An edited item: its centre as shares of the screen's width and height (0..1) and its size (1 = the default). */
data class Place(val x: Float, val y: Float, val scale: Float = 1f)

/**
 * Where the game picture and the touch buttons are, in pixels, on a [width] × [height] screen. With no [edits] it is
 * the layout of 4c: upright (layout A) the picture full width at the top (3:2) and the buttons in the area below it;
 * sideways the picture's box is the whole screen (the game fitted inside) and the buttons over the side bars —
 * ⟲ rewind and ⏩ fast-forward (KeyMap.REWIND / FAST, not GBA buttons) upright between L and R, sideways just below L
 * and R. [edits] (the layout editor, 6c) move items anywhere and size them ½×–2×, always inside the screen.
 * [fitMode] (Settings.SIZE_*) places the game itself inside the picture's box ([game], 8b).
 * Pure maths (no Android classes), so it is tested on the PC.
 */
class TouchLayout(val width: Float, val height: Float, private val density: Float, val sideways: Boolean,
                  val edits: Map<LayoutItem, Place> = emptyMap(), val fitMode: Int = Settings.SIZE_FIT) {
    data class Box(val left: Float, val top: Float, val right: Float, val bottom: Float) {
        val cx get() = (left + right) / 2
        val cy get() = (top + bottom) / 2

        fun overlaps(o: Box) = left < o.right && o.left < right && top < o.bottom && o.top < bottom
    }

    /** One button: [round] ones are the circle inside the box, the others a rounded box; [scale] its size. */
    data class Button(val bit: Int, val label: String, val round: Boolean,
                      val left: Float, val top: Float, val right: Float, val bottom: Float, val scale: Float = 1f) {
        val cx get() = (left + right) / 2
        val cy get() = (top + bottom) / 2
        val box get() = Box(left, top, right, bottom)
    }

    private val dp = density
    private val slop = 8 * dp // fingers are imprecise: a button reacts a little outside its drawing

    val picture: Box
    val dpadX: Float
    val dpadY: Float
    val dpadRadius: Float
    val buttons: List<Button>
    private val scales = HashMap<LayoutItem, Float>()

    init {
        val defaults = defaultBoxes()
        picture = placed(LayoutItem.PICTURE, defaults.getValue(LayoutItem.PICTURE))
        val dpad = placed(LayoutItem.DPAD, defaults.getValue(LayoutItem.DPAD))
        dpadX = dpad.cx
        dpadY = dpad.cy
        dpadRadius = (dpad.right - dpad.left) / 2
        buttons = BUTTONS.map { (item, bit, label) ->
            val b = placed(item, defaults.getValue(item))
            Button(bit, label, bit == KeyMap.A || bit == KeyMap.B, b.left, b.top, b.right, b.bottom, scales.getValue(item))
        }
    }

    /** 4c's boxes: upright the area below the picture, sideways the whole screen. */
    private fun defaultBoxes(): Map<LayoutItem, Box> {
        val m = 16 * dp
        val boxes = HashMap<LayoutItem, Box>()
        if (!sideways) {
            val top = width * 2 / 3 // the picture: full width, 3:2, at the top
            boxes[LayoutItem.PICTURE] = Box(0f, 0f, width, top)
            val h = height - top // the controls area below it
            val shoulderH = 40 * dp
            val smallW = 80 * dp
            val smallH = 32 * dp
            val smallTop = h - m - smallH
            val r = 70 * dp
            val dx = m + r
            val dy = (m + shoulderH + smallTop) / 2
            boxes[LayoutItem.DPAD] = Box(dx - r, top + dy - r, dx + r, top + dy + r)
            val ar = 36 * dp
            val ax = width - m - ar
            val ay = dy - 28 * dp
            val bx = ax - 84 * dp
            val by = ay + 56 * dp
            val extraW = 64 * dp
            val extraTop = m + 4 * dp
            val extraBottom = m + shoulderH - 4 * dp
            boxes[LayoutItem.L] = Box(0f, top + m, 96 * dp, top + m + shoulderH)
            boxes[LayoutItem.R] = Box(width - 96 * dp, top + m, width, top + m + shoulderH)
            boxes[LayoutItem.REWIND] = Box(width / 2 - 8 * dp - extraW, top + extraTop, width / 2 - 8 * dp, top + extraBottom)
            boxes[LayoutItem.FAST] = Box(width / 2 + 8 * dp, top + extraTop, width / 2 + 8 * dp + extraW, top + extraBottom)
            boxes[LayoutItem.A] = Box(ax - ar, top + ay - ar, ax + ar, top + ay + ar)
            boxes[LayoutItem.B] = Box(bx - ar, top + by - ar, bx + ar, top + by + ar)
            boxes[LayoutItem.SELECT] = Box(width / 2 - 8 * dp - smallW, top + smallTop, width / 2 - 8 * dp, top + smallTop + smallH)
            boxes[LayoutItem.START] = Box(width / 2 + 8 * dp, top + smallTop, width / 2 + 8 * dp + smallW, top + smallTop + smallH)
        } else {
            boxes[LayoutItem.PICTURE] = Box(0f, 0f, width, height) // the game is fitted inside
            val shoulderH = 40 * dp
            val smallW = 72 * dp
            val smallH = 28 * dp
            val smallTop = height - m - smallH
            val r = 60 * dp
            val dx = m + r
            val dy = height * 0.55f
            boxes[LayoutItem.DPAD] = Box(dx - r, dy - r, dx + r, dy + r)
            val ar = 30 * dp
            val ax = width - m - ar
            val ay = height * 0.45f
            val bx = ax - 66 * dp
            val by = ay + 52 * dp
            val extraTop = m + shoulderH + 12 * dp
            val extraBottom = extraTop + 32 * dp
            boxes[LayoutItem.L] = Box(0f, m, 96 * dp, m + shoulderH)
            boxes[LayoutItem.R] = Box(width - 96 * dp, m, width, m + shoulderH)
            boxes[LayoutItem.REWIND] = Box(16 * dp, extraTop, 80 * dp, extraBottom)
            boxes[LayoutItem.FAST] = Box(width - 80 * dp, extraTop, width - 16 * dp, extraBottom)
            boxes[LayoutItem.A] = Box(ax - ar, ay - ar, ax + ar, ay + ar)
            boxes[LayoutItem.B] = Box(bx - ar, by - ar, bx + ar, by + ar)
            boxes[LayoutItem.SELECT] = Box(width / 2 - 8 * dp - smallW, smallTop, width / 2 - 8 * dp, smallTop + smallH)
            boxes[LayoutItem.START] = Box(width / 2 + 8 * dp, smallTop, width / 2 + 8 * dp + smallW, smallTop + smallH)
        }
        return boxes
    }

    /** [default] moved and sized by the item's edit, kept inside the screen (never bigger than it). */
    private fun placed(item: LayoutItem, default: Box): Box {
        val e = edits[item]
        val w0 = default.right - default.left
        val h0 = default.bottom - default.top
        val s = min(clampScale(e?.scale ?: 1f), min(width / w0, height / h0))
        scales[item] = s
        val w = w0 * s
        val h = h0 * s
        val cx = (e?.x?.times(width) ?: default.cx).coerceIn(w / 2, width - w / 2)
        val cy = (e?.y?.times(height) ?: default.cy).coerceIn(h / 2, height - h / 2)
        return Box(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
    }

    /** The same layout with [item] at [place] (its size brought into ½–2). */
    fun with(item: LayoutItem, place: Place): TouchLayout =
        TouchLayout(width, height, density, sideways, edits + (item to place.copy(scale = clampScale(place.scale))), fitMode)

    /** Where [item] is now, as a Place (for the editor). */
    fun place(item: LayoutItem): Place {
        val b = box(item)
        return Place(b.cx / width, b.cy / height, scales.getValue(item))
    }

    fun box(item: LayoutItem): Box = when (item) {
        LayoutItem.PICTURE -> picture
        LayoutItem.DPAD -> Box(dpadX - dpadRadius, dpadY - dpadRadius, dpadX + dpadRadius, dpadY + dpadRadius)
        else -> buttons[BUTTONS.indexOfFirst { it.first == item }].box
    }

    /** Over the game picture's box (sideways: the whole screen). Plain draws these see-through, as before 8b. */
    fun overPicture(b: Button) = b.box.overlaps(picture)
    val dpadOverPicture get() = box(LayoutItem.DPAD).overlaps(picture)

    /** Where the game itself is drawn inside the picture's box (the rest is the skin's body). */
    val game: Box get() = gameIn(picture, fitMode)

    /** Over the game itself: the skins (8b) draw these see-through, the others solid on the body. */
    fun overGame(b: Button) = b.box.overlaps(game)
    val dpadOverGame get() = box(LayoutItem.DPAD).overlaps(game)

    /** The editor: the item under a finger at (x, y) — a button, else the D-pad, else the picture — or null. */
    fun itemAt(x: Float, y: Float): LayoutItem? {
        buttons.forEachIndexed { i, b -> if (inside(b, x, y, 0f)) return BUTTONS[i].first }
        if (hypot(x - dpadX, y - dpadY) <= dpadRadius) return LayoutItem.DPAD
        if (x >= picture.left && x <= picture.right && y >= picture.top && y <= picture.bottom) return LayoutItem.PICTURE
        return null
    }

    /** The GBA buttons a finger at (x, y) presses: D-pad directions, or the nearest button it touches, or 0. */
    fun buttonsAt(x: Float, y: Float): Int {
        val dx = x - dpadX
        val dy = y - dpadY
        val dist = hypot(dx, dy)
        if (dist <= dpadRadius + slop) return if (dist < dpadRadius * DEAD_ZONE) 0 else dpadDirection(dx, dy)
        var best = 0
        var bestDist = Float.MAX_VALUE
        for (b in buttons) {
            if (!inside(b, x, y, slop)) continue
            val d = hypot(x - b.cx, y - b.cy)
            if (d < bestDist) {
                best = b.bit
                bestDist = d
            }
        }
        return best
    }

    private fun inside(b: Button, x: Float, y: Float, margin: Float): Boolean =
        if (b.round) hypot(x - b.cx, y - b.cy) <= (b.right - b.left) / 2 + margin
        else x >= b.left - margin && x <= b.right + margin && y >= b.top - margin && y <= b.bottom + margin

    companion object {
        const val DEAD_ZONE = 0.25f // no direction in the middle quarter of the D-pad's radius
        const val MIN_SCALE = 0.5f
        const val MAX_SCALE = 2f

        /** The buttons in drawing order: the item, its bit, its label. */
        private val BUTTONS = listOf(
            Triple(LayoutItem.L, KeyMap.L, "L"), Triple(LayoutItem.R, KeyMap.R, "R"),
            Triple(LayoutItem.REWIND, KeyMap.REWIND, "⟲"), Triple(LayoutItem.FAST, KeyMap.FAST, "⏩"),
            Triple(LayoutItem.A, KeyMap.A, "A"), Triple(LayoutItem.B, KeyMap.B, "B"),
            Triple(LayoutItem.SELECT, KeyMap.SELECT, "SELECT"), Triple(LayoutItem.START, KeyMap.START, "START"),
        )

        fun clampScale(s: Float) = s.coerceIn(MIN_SCALE, MAX_SCALE)

        /** The game inside [box], as OpenGL draws it on a surface of the box's size (fit_rect_mode, platform/android/fit_rect.c). */
        fun gameIn(box: Box, fitMode: Int): Box {
            val w = (box.right - box.left).roundToInt()
            val h = (box.bottom - box.top).roundToInt()
            if (w <= 0 || h <= 0 || fitMode == Settings.SIZE_STRETCH) return box
            fun centred(gw: Int, gh: Int): Box {
                val x = box.left + (w - gw) / 2
                val y = box.top + (h - gh) / 2
                return Box(x, y, x + gw, y + gh)
            }
            if (fitMode == Settings.SIZE_INTEGER) {
                val n = min(w / 240, h / 160)
                if (n >= 1) return centred(240 * n, 160 * n)
            }
            return if (w * 2 <= h * 3) centred(w, w * 2 / 3) else centred(h * 3 / 2, h)
        }

        /** 8 sectors of 45°, from the D-pad's centre (screen y grows downwards): diagonals press two directions. */
        fun dpadDirection(dx: Float, dy: Float): Int {
            val angle = Math.toDegrees(atan2(-dy, dx).toDouble()) // 0 = right, 90 = up
            return when (((angle / 45.0).roundToInt() + 8) % 8) {
                0 -> KeyMap.RIGHT
                1 -> KeyMap.RIGHT or KeyMap.UP
                2 -> KeyMap.UP
                3 -> KeyMap.UP or KeyMap.LEFT
                4 -> KeyMap.LEFT
                5 -> KeyMap.LEFT or KeyMap.DOWN
                6 -> KeyMap.DOWN
                else -> KeyMap.DOWN or KeyMap.RIGHT
            }
        }
    }
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: |BUILD"
ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'
```

Expected: `BUILD SUCCESSFUL`; `96 0`.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/TouchLayout.kt app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-8b-console-skins.md
git commit -m "feat(app): the touch layout knows where the game itself is drawn" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 5: The buttons in the skin, the body round the picture, the Skin page

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt` (`skin`, `fitMode`; colours from the skin, darker when pressed, the rim on the body, see-through over the game — Plain over the picture's box, as before; ⏩ drawn as two triangles in the label's colour)
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt` (`GameScreen(skin, fitMode)`: the body colour behind the game, both passed to the view)
- Modify: `app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt` (`skin`, `fitMode`: the editor in the skin)
- Modify: `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt` (`SettingsPage.SKIN`, `SettingsPage.up`; the Skin row; the summary with the skin's name; `SkinGrid`, `SkinTile`, `drawSkinPhone`)
- Modify: `app/src/main/java/com/tengen/gbaemu/MainActivity.kt` (`setBackground` with `setPicture`; the skin and the size mode to the game screen and the editor; Back through `SettingsPage.up`)
- Test: `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt` (+1: `theSkinRowShowsTheSkinAndTheSkinPagePicksOne`)

- [x] **Step 1: Write the failing test**

Replace `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** The screens of milestone 8a's new look and 8b's skins: what they show and what tapping them does. */
@RunWith(AndroidJUnit4::class)
class ScreensTest {
    @get:Rule
    val compose = createComposeRule()

    @Test
    fun thePauseMenuShowsResumeTheTilesAndPicksASpeed() {
        var picked = 0
        var resumed = false
        compose.setContent {
            AppTheme {
                GameMenu("game C", 4, onFastSpeed = { picked = it }, onResume = { resumed = true }, onSave = {}, onLoad = {},
                    onCheats = {}, onSettings = {}, onRestart = {}, onQuit = {})
            }
        }
        for (label in listOf("game C", "Resume", "Save state", "Load state", "Cheats", "Settings", "Restart", "Quit")) {
            compose.onNodeWithText(label).assertIsDisplayed()
        }
        compose.onNodeWithText("8×").performClick()
        assertEquals(8, picked)
        compose.onNodeWithText("Max").performClick()
        assertEquals(NativeBridge.FF_MAX, picked)
        compose.onNodeWithText("Resume").performClick()
        assertEquals(true, resumed)
    }

    @Test
    fun aSettingsSwitchAndChoiceChangeTheirSetting() {
        var changed = Settings()
        compose.setContent {
            AppTheme {
                SettingsScreen(SettingsPage.PICTURE, Settings(), KeyBindings(MemoryStore()), 0, "Built-in",
                    onPage = {}, onBack = {}, onChange = { changed = it }, onPickKey = { _, _ -> }, onResetKeys = {},
                    onChooseBios = {}, onBuiltInBios = {})
            }
        }
        compose.onNodeWithText("Show speed").performClick()
        assertEquals(true, changed.showSpeed)
        compose.onNodeWithText("Smooth").performClick()
        assertEquals(true, changed.smooth)
        compose.onNodeWithText("Whole steps").performClick()
        assertEquals(Settings.SIZE_NAMES.indexOf("Whole-number steps"), changed.size)
    }

    @Test
    fun theSkinRowShowsTheSkinAndTheSkinPagePicksOne() {
        var page: SettingsPage? = null
        var changed = Settings()
        val content = mutableStateOf(SettingsPage.TOUCH)
        compose.setContent {
            AppTheme {
                SettingsScreen(content.value, Settings(skin = 5), KeyBindings(MemoryStore()), 0, "Built-in",
                    onPage = { page = it }, onBack = {}, onChange = { changed = it }, onPickKey = { _, _ -> }, onResetKeys = {},
                    onChooseBios = {}, onBuiltInBios = {})
            }
        }
        compose.onNodeWithText("Black").assertIsDisplayed() // the Skin row's summary
        compose.onNodeWithText("Skin").performClick()
        assertEquals(SettingsPage.SKIN, page)
        content.value = SettingsPage.SKIN
        compose.onNodeWithText("Classic grey").assertExists()
        compose.onNodeWithText("Ice").performClick()
        assertEquals(1, changed.skin)
        assertEquals("ice", Skins.at(changed.skin).id)
    }

    @Test
    fun theGameListShowsTheContinueCardAndOpensAGamesSheetFromItsButton() {
        val older = GameEntry("a", "u", "game B", lastPlayed = 100)
        val newest = GameEntry("b", "u", "game A", lastPlayed = 300)
        val never = GameEntry("c", "u", "game D")
        var held: GameEntry? = null
        var played: GameEntry? = null
        compose.setContent {
            AppTheme {
                LibraryScreen(listOf(newest, older, never), "GBA", scanning = false, message = null, refresh = 0,
                    picture = { null }, onPlay = { played = it }, onHold = { held = it }, onOpenGame = {}, onChangeFolder = {},
                    onSettings = {})
            }
        }
        compose.onNodeWithTag("continue").assertIsDisplayed()
        compose.onNode(hasText("game A")).assertIsDisplayed()
        compose.onNodeWithText("Continue").assertIsDisplayed()
        compose.onNodeWithContentDescription("game B: more").performClick()
        assertEquals(older, held)
        compose.onNodeWithTag("continue").performClick()
        assertEquals(newest, played)
    }
}
```

- [x] **Step 2: Run it to see it fail**

```bash
bash tools/dev.sh androidtest 2>&1 | grep -E "^e: " | head -2
```

Expected: FAIL to compile — `Unresolved reference 'SKIN'`.

- [x] **Step 3: Write the code**

Replace `app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt`:

```kotlin
package com.tengen.gbaemu

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Rect
import android.graphics.RectF
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * The on-screen GBA buttons over the whole screen, placed by [TouchLayout] (the default layout, or the user's [edits]).
 * Every finger is followed; the pressed buttons go to [onMask] whenever they change, each newly pressed one with a
 * vibration tick (Android's own touch feedback, so the phone's setting applies) unless [buzz] is off. The colours are
 * the [skin]'s (8b): buttons over the game picture see-through ([look]: Settings.LOOK_*), the others solid with the
 * skin's rim. While [hidden] (a keyboard or gamepad is in use) nothing is drawn; the next touch shows the buttons again.
 *
 * [editing] (the layout editor): nothing is pressed; a finger picks the item under it ([onSelect]) and drags it
 * ([onEdit] with the new edits); the picture is drawn too ([picture], or a plain block) and the picked one outlined.
 */
class TouchControlsView(context: Context) : View(context) {
    /** Which default layout: sideways (buttons over the side bars) or upright (the area below the picture). */
    var sideways = false
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }

    /** The user's layout (6c): items moved and sized; empty = the default. */
    var edits: Map<LayoutItem, Place> = emptyMap()
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }

    var hidden = false
        set(value) {
            if (field == value) return
            field = value
            if (value) setMask(0)
            invalidate()
        }

    /** Fast mode switched on: ⏩ is drawn lit. */
    var fastOn = false
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }

    var look = Settings.LOOK_NORMAL
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }

    /** The colours (8b); the body round the picture is drawn behind this view (the game screen, OpenGL). */
    var skin: Skin = Skins.PLAIN
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }

    /** The picture's size mode (Settings.SIZE_*): where the game is drawn, so which buttons are over it. */
    var fitMode = Settings.SIZE_FIT
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }

    var buzz = true

    var onMask: (Int) -> Unit = {}
    var onShow: () -> Unit = {}

    var editing = false
    var selected: LayoutItem? = null
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }
    var picture: Bitmap? = null // the editor: the paused game's picture
        set(value) {
            field = value
            invalidate()
        }
    var onSelect: (LayoutItem?) -> Unit = {}
    var onEdit: (Map<LayoutItem, Place>) -> Unit = {}

    var touchLayout: TouchLayout? = null
        private set

    private var mask = 0
    private var ignoreUntilUp = false // the touch that showed the buttons presses nothing
    private var dragStart: Place? = null // the editor: where the dragged item was when the finger came down
    private var downX = 0f
    private var downY = 0f

    private val density = resources.displayMetrics.density
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }
    private val outline = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = 0xFFF2A33A.toInt() // the app's amber (AppTheme)
        strokeWidth = 2 * resources.displayMetrics.density
        pathEffect = DashPathEffect(floatArrayOf(10f, 8f), 0f)
    }
    private val rim = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1.5f * resources.displayMetrics.density
    }

    private val dpadShape = Path() // the cross as one shape, so see-through arms do not overlap
    private val fastShape = Path()

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) = relayout()

    private fun relayout() {
        val l = if (width > 0 && height > 0) TouchLayout(width.toFloat(), height.toFloat(), density, sideways, edits, fitMode) else null
        touchLayout = l
        dpadShape.reset()
        if (l != null) {
            val r = l.dpadRadius
            val a = r * ARM
            val across = Path().apply { addRoundRect(l.dpadX - r, l.dpadY - a, l.dpadX + r, l.dpadY + a, a / 3, a / 3, Path.Direction.CW) }
            val down = Path().apply { addRoundRect(l.dpadX - a, l.dpadY - r, l.dpadX + a, l.dpadY + r, a / 3, a / 3, Path.Direction.CW) }
            dpadShape.op(across, down, Path.Op.UNION)
        }
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility") // game buttons, not clicks
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val layout = touchLayout ?: return false
        val action = event.actionMasked
        if (editing) return edit(layout, event)
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
        var pressed = 0
        if (action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
            val lifted = if (action == MotionEvent.ACTION_POINTER_UP) event.actionIndex else -1
            for (i in 0 until event.pointerCount) {
                if (i != lifted) pressed = pressed or layout.buttonsAt(event.getX(i), event.getY(i))
            }
        }
        setMask(pressed)
        return true
    }

    /** The editor: a finger picks the item under it and drags it (the first finger only). */
    private fun edit(layout: TouchLayout, event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val item = layout.itemAt(event.x, event.y)
                selected = item
                onSelect(item)
                dragStart = item?.let(layout::place)
                downX = event.x
                downY = event.y
            }
            MotionEvent.ACTION_MOVE -> {
                val item = selected ?: return true
                val start = dragStart ?: return true
                val moved = layout.with(item, start.copy(x = start.x + (event.x - downX) / width, y = start.y + (event.y - downY) / height))
                onEdit(edits + (item to moved.place(item))) // kept inside the screen
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> dragStart = null
        }
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
        val layout = touchLayout ?: return
        if (hidden && !editing) return
        if (editing) drawPicture(canvas, layout)
        // On the body: the skin's solid colours, darker when pressed, with its rim. Over the game picture: see-through
        // (Faint / Normal / Strong) in the skin's over-picture colours, or each part's own (Plain: white, as before 8b).
        val (idleA, downA, textA) = when (look) {
            Settings.LOOK_FAINT -> Triple(0x33, 0x66, 0x88)
            Settings.LOOK_STRONG -> Triple(0x99, 0xCC, 0xFF)
            else -> Triple(0x55, 0x99, 0xDD)
        }
        val s = skin
        fun alpha(c: Int, a: Int) = (a shl 24) or (c and 0xFFFFFF)
        fun colours(part: Int, ink: Int, over: Boolean) = if (over) {
            val c = s.overPicture ?: part
            Triple(alpha(c, idleA), alpha(c, downA), alpha(s.overPictureText ?: ink, textA))
        } else {
            Triple(part, Skins.pressed(part), ink)
        }
        rim.color = s.rim ?: 0

        // D-pad: the cross, then each pressed arm (outside the middle) again, darker (see-through: lighter over it).
        val dpadOver = if (s.seeThroughBesideGame) layout.dpadOverPicture else layout.dpadOverGame
        val (dIdle, dDown, _) = colours(s.dpad, 0, dpadOver)
        fill.color = dIdle
        canvas.drawPath(dpadShape, fill)
        if (s.rim != null && !dpadOver) canvas.drawPath(dpadShape, rim)
        val x = layout.dpadX
        val y = layout.dpadY
        val r = layout.dpadRadius
        val a = r * ARM
        fill.color = if (dpadOver) dIdle else dDown
        if (mask and KeyMap.UP != 0) canvas.drawRoundRect(x - a, y - r, x + a, y - a, a / 3, a / 3, fill)
        if (mask and KeyMap.DOWN != 0) canvas.drawRoundRect(x - a, y + a, x + a, y + r, a / 3, a / 3, fill)
        if (mask and KeyMap.LEFT != 0) canvas.drawRoundRect(x - r, y - a, x - a, y + a, a / 3, a / 3, fill)
        if (mask and KeyMap.RIGHT != 0) canvas.drawRoundRect(x + a, y - a, x + r, y + a, a / 3, a / 3, fill)

        for (b in layout.buttons) {
            val over = if (s.seeThroughBesideGame) layout.overPicture(b) else layout.overGame(b)
            val (part, partInk) = when (b.bit) {
                KeyMap.A, KeyMap.B -> s.ab to s.abText
                KeyMap.SELECT, KeyMap.START -> s.small to s.smallText
                else -> s.shoulder to s.shoulderText // L, R, rewind, fast-forward
            }
            val (idle, down, ink) = colours(part, partInk, over)
            val lit = b.bit == KeyMap.FAST && fastOn && mask and b.bit == 0
            fill.color = when {
                mask and b.bit != 0 -> down
                lit -> s.lit
                else -> idle
            }
            val edge = s.rim != null && !over
            if (b.round) {
                canvas.drawCircle(b.cx, b.cy, (b.right - b.left) / 2, fill)
                if (edge) canvas.drawCircle(b.cx, b.cy, (b.right - b.left) / 2, rim)
            } else {
                val corner = (b.bottom - b.top) / 2
                canvas.drawRoundRect(b.left, b.top, b.right, b.bottom, corner, corner, fill)
                if (edge) canvas.drawRoundRect(b.left, b.top, b.right, b.bottom, corner, corner, rim)
            }
            text.color = if (lit) s.litText else ink
            text.textSize = b.scale * when {
                b.round -> 22 * density
                b.bit == KeyMap.FAST || b.bit == KeyMap.REWIND -> 20 * density // symbols, not words
                else -> 13 * density
            }
            if (b.bit == KeyMap.FAST) drawFastForward(canvas, b) // ⏩ is a colour emoji: drawn, so it takes the skin's colour
            else canvas.drawText(b.label, b.cx, b.cy - (text.ascent() + text.descent()) / 2, text)
        }
        if (editing) selected?.let { item ->
            val box = layout.box(item)
            val pad = 4 * density
            canvas.drawRect(box.left - pad, box.top - pad, box.right + pad, box.bottom + pad, outline)
        }
    }

    /** The fast-forward symbol: two triangles pointing right, in the label's colour. */
    private fun drawFastForward(canvas: Canvas, b: TouchLayout.Button) {
        val h = 14 * density * b.scale
        val w = h * 0.8f
        fastShape.reset()
        for (i in 0..1) {
            val x = b.cx - w + i * w
            fastShape.moveTo(x, b.cy - h / 2)
            fastShape.lineTo(x + w, b.cy)
            fastShape.lineTo(x, b.cy + h / 2)
            fastShape.close()
        }
        fill.color = text.color
        canvas.drawPath(fastShape, fill)
    }

    /** The editor: the paused game's picture fitted in the picture's box, or a plain block with its name. */
    private fun drawPicture(canvas: Canvas, layout: TouchLayout) {
        val box = layout.picture
        val frame = picture
        if (frame != null) {
            val fit = run { // 3:2 inside the box
                val w = box.right - box.left
                val h = box.bottom - box.top
                val fw = if (w * 2 <= h * 3) w else h * 3 / 2
                val fh = fw * 2 / 3
                RectF(box.left + (w - fw) / 2, box.top + (h - fh) / 2, box.left + (w + fw) / 2, box.top + (h + fh) / 2)
            }
            canvas.drawBitmap(frame, Rect(0, 0, frame.width, frame.height), fit, null)
        } else {
            fill.color = PICTURE_BLOCK
            canvas.drawRect(box.left, box.top, box.right, box.bottom, fill)
            text.color = 0xFF9C998F.toInt()
            text.textSize = 16 * density
            canvas.drawText("Game picture", box.cx, box.cy, text)
        }
    }

    private companion object {
        const val ARM = 0.36f // the D-pad's arm half-width, as a part of its radius
        const val PICTURE_BLOCK = 0xFF2A2C31.toInt()
    }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/Screens.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Context
import android.graphics.Bitmap
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ExitToApp
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.FileDownload
import androidx.compose.material.icons.filled.FileUpload
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.RemoveCircleOutline
import androidx.compose.material.icons.filled.Replay
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.SportsEsports
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.WbSunny
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import java.text.DateFormat
import java.util.Calendar
import java.util.Date
import kotlin.math.roundToInt
import kotlinx.coroutines.delay

/** The very first start (no games folder yet) or a folder that can't be opened any more. */
@Composable
fun FirstStartScreen(folderLost: Boolean, message: String?, onChooseFolder: () -> Unit, onOpenGame: () -> Unit,
                     onSettings: () -> Unit) {
    Surface(modifier = Modifier.fillMaxSize()) { /* the theme's background and text colours */
        Box(Modifier.fillMaxSize().systemBarsPadding()) {
            IconButton(onClick = onSettings, modifier = Modifier.align(Alignment.TopEnd).padding(4.dp)) {
                Icon(Icons.Filled.Settings, contentDescription = "Settings")
            }
            Column(
                modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
                verticalArrangement = Arrangement.Center,
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Icon(Icons.Filled.SportsEsports, contentDescription = null, tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(56.dp))
                Spacer(Modifier.height(12.dp))
                Text("GBA Emulator", style = MaterialTheme.typography.headlineMedium)
                Spacer(Modifier.height(16.dp))
                Text(
                    if (folderLost) "Can't open the games folder any more. Choose it again."
                    else "Pick the folder where your games are. New games you put there show up by themselves.\n\n" +
                        "Android doesn't allow the Download folder itself: make a folder such as GBA (the picker's " +
                        "Create new folder) and put your games in it.",
                    textAlign = TextAlign.Center,
                    color = if (folderLost) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.widthIn(max = 420.dp),
                )
                Spacer(Modifier.height(28.dp))
                Button(onClick = onChooseFolder, modifier = Modifier.widthIn(min = 240.dp).heightIn(min = 52.dp)) {
                    Icon(Icons.Filled.FolderOpen, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Choose games folder")
                }
                Spacer(Modifier.height(12.dp))
                OutlinedButton(onClick = onOpenGame, modifier = Modifier.widthIn(min = 240.dp).heightIn(min = 52.dp)) {
                    Text("Open one game")
                }
                if (message != null) {
                    Spacer(Modifier.height(16.dp))
                    Text(message, color = MaterialTheme.colorScheme.error, textAlign = TextAlign.Center)
                }
            }
        }
    }
}

/** When a game was last played, as the list shows it: "Today 14:00", "Yesterday 09:12", a date, or "Never played". */
fun playedText(lastPlayed: Long, now: Long = System.currentTimeMillis()): String {
    if (lastPlayed <= 0) return "Never played"
    val day = { t: Long -> Calendar.getInstance().apply { timeInMillis = t }.let { it.get(Calendar.YEAR) * 1000 + it.get(Calendar.DAY_OF_YEAR) } }
    val time = DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(lastPlayed))
    val yesterday = Calendar.getInstance().apply { timeInMillis = now; add(Calendar.DAY_OF_YEAR, -1) }.timeInMillis
    return when (day(lastPlayed)) {
        day(now) -> "Today $time"
        day(yesterday) -> "Yesterday $time"
        else -> DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(lastPlayed))
    }
}

/**
 * The game list: the last-played game as a big Continue card, then a grid of cards (the picture of where each was
 * left, its name, when it was last played). A tap plays; holding or the card's ⋮ opens [onHold]'s sheet. [refresh]
 * changes whenever the pictures may have changed.
 */
@Composable
fun LibraryScreen(rows: List<GameEntry>, folderName: String?, scanning: Boolean, message: String?, refresh: Int,
                  picture: (GameEntry) -> Bitmap?, onPlay: (GameEntry) -> Unit, onHold: (GameEntry) -> Unit,
                  onOpenGame: () -> Unit, onChangeFolder: () -> Unit, onSettings: () -> Unit) {
    var menuOpen by remember { mutableStateOf(false) }
    Surface(modifier = Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().systemBarsPadding()) {
            ScreenHeader("Your games", subtitle = folderName) {
                IconButton(onClick = onSettings) { Icon(Icons.Filled.Settings, contentDescription = "Settings") }
                Box {
                    IconButton(onClick = { menuOpen = true }) { Icon(Icons.Filled.MoreVert, contentDescription = "More") }
                    DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                        DropdownMenuItem(text = { Text("Open one game") }, onClick = { menuOpen = false; onOpenGame() })
                        DropdownMenuItem(text = { Text("Change folder") }, onClick = { menuOpen = false; onChangeFolder() })
                    }
                }
            }
            if (message != null) {
                Text(message, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp))
            }
            if (rows.isEmpty()) {
                Column(Modifier.fillMaxSize().padding(24.dp), verticalArrangement = Arrangement.Center,
                    horizontalAlignment = Alignment.CenterHorizontally) {
                    if (scanning) {
                        CircularProgressIndicator()
                        Spacer(Modifier.height(16.dp))
                        Text("Looking for games", color = MaterialTheme.colorScheme.onSurfaceVariant)
                    } else {
                        Text("No games in ${folderName ?: "the folder"} yet", style = MaterialTheme.typography.titleMedium,
                            textAlign = TextAlign.Center)
                        Spacer(Modifier.height(8.dp))
                        Text("Put .gba or .zip files there, or pick another folder.", textAlign = TextAlign.Center,
                            color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Spacer(Modifier.height(20.dp))
                        Button(onClick = onChangeFolder) { Text("Change folder") }
                    }
                }
            } else {
                val (last, rest) = remember(rows) { Library.continueAndRest(rows) }
                LazyVerticalGrid(
                    columns = GridCells.Adaptive(150.dp), modifier = Modifier.fillMaxSize(),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(start = 12.dp, end = 12.dp, bottom = 16.dp),
                    horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    if (last != null) {
                        item(key = "continue:" + last.key, span = { GridItemSpan(maxLineSpan) }) {
                            GameCard(last, refresh, picture, onPlay, onHold, big = true)
                        }
                    }
                    items(rest, key = { it.key }) { game -> GameCard(game, refresh, picture, onPlay, onHold, big = false) }
                }
            }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun GameCard(game: GameEntry, refresh: Int, picture: (GameEntry) -> Bitmap?, onPlay: (GameEntry) -> Unit,
                     onHold: (GameEntry) -> Unit, big: Boolean) {
    val bitmap = remember(game.id, refresh) { picture(game) }
    Surface(
        shape = MaterialTheme.shapes.medium,
        color = if (big) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surfaceContainer,
        border = cardBorder(big),
        contentColor = MaterialTheme.colorScheme.onSurface,
        modifier = Modifier.fillMaxWidth().clip(MaterialTheme.shapes.medium)
            .combinedClickable(onClick = { onPlay(game) }, onLongClick = { onHold(game) })
            .testTag(if (big) "continue" else "game"),
    ) {
        // Wide (the Continue card sideways or on a tablet): the picture on the left at a fixed size, the text beside it.
        BoxWithConstraints(Modifier.padding(8.dp)) {
            if (big && maxWidth > 560.dp) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    GamePicture(bitmap, big, Modifier.width(300.dp))
                    Spacer(Modifier.width(12.dp))
                    GameInfo(game, big, onHold, Modifier.weight(1f))
                }
            } else {
                Column {
                    GamePicture(bitmap, big, Modifier.fillMaxWidth())
                    GameInfo(game, big, onHold, Modifier.padding(top = 6.dp))
                }
            }
        }
    }
}

/** A game's picture of where it was left (3:2, sharp pixels), or a placeholder. */
@Composable
private fun GamePicture(bitmap: Bitmap?, big: Boolean, modifier: Modifier) {
    val frame = modifier.aspectRatio(1.5f).clip(RoundedCornerShape(8.dp))
    if (bitmap != null) {
        Image(bitmap.asImageBitmap(), contentDescription = null, modifier = frame,
            contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.None)
    } else {
        Box(frame.background(MaterialTheme.colorScheme.surfaceContainerHighest), contentAlignment = Alignment.Center) {
            Icon(Icons.Filled.SportsEsports, contentDescription = null, tint = MaterialTheme.colorScheme.outline,
                modifier = Modifier.size(if (big) 48.dp else 32.dp))
        }
    }
}

/** A game's name, when it was last played (with the Continue tag on the big card) and its ⋮. */
@Composable
private fun GameInfo(game: GameEntry, big: Boolean, onHold: (GameEntry) -> Unit, modifier: Modifier) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = modifier) {
        Column(Modifier.weight(1f).padding(start = 4.dp)) {
            Text(game.name, maxLines = 2, overflow = TextOverflow.Ellipsis,
                style = if (big) MaterialTheme.typography.titleMedium else MaterialTheme.typography.bodyMedium)
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 2.dp)) {
                if (big) {
                    Row(Modifier.clip(RoundedCornerShape(8.dp)).background(MaterialTheme.colorScheme.primaryContainer)
                        .padding(horizontal = 8.dp, vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Filled.PlayArrow, contentDescription = null, tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(14.dp))
                        Text("Continue", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
                    }
                    Spacer(Modifier.width(8.dp))
                }
                Text(playedText(game.lastPlayed) + if (game.outside) " · outside the folder" else "",
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
        IconButton(onClick = { onHold(game) }) {
            Icon(Icons.Filled.MoreVert, contentDescription = "${game.name}: more", tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

/** A game's sheet (holding it, or its ⋮). [onRemove] only for games from outside the folder. */
@Composable
fun GameHoldMenu(game: GameEntry, onStartFresh: () -> Unit, onImport: () -> Unit, onExport: () -> Unit,
                 onGameSettings: () -> Unit, onRemove: (() -> Unit)?, onClose: () -> Unit) {
    ActionSheet(game.name, listOfNotNull(
        SheetItem(Icons.Filled.Replay, "Start fresh (from the title screen)", onClick = onStartFresh),
        SheetItem(Icons.Filled.FileDownload, "Import save", onClick = onImport),
        SheetItem(Icons.Filled.FileUpload, "Export save", onClick = onExport),
        SheetItem(Icons.Filled.Tune, "Game settings", onClick = onGameSettings),
        onRemove?.let { SheetItem(Icons.Filled.RemoveCircleOutline, "Remove from list", danger = true, onClick = it) },
    ), onClose)
}

/** A text button in the error colour, for what can't be undone. */
@Composable
fun DangerButton(label: String, onClick: () -> Unit) {
    TextButton(onClick = onClick, colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error)) {
        Text(label)
    }
}

/** Import over a save that exists: the old one is kept as .sav.bak. */
@Composable
fun ReplaceSaveDialog(name: String, onReplace: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Replace this game's save?") },
        text = { Text("$name already has a save. It will be kept as a backup, and the game will start from the title screen with the imported save.") },
        confirmButton = { DangerButton("Replace", onReplace) },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** A game with a save chip and no save yet: start fresh, or bring in a .sav file. Dismissing cancels the opening. */
@Composable
fun ImportDialog(error: String?, onStartFresh: () -> Unit, onImport: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("No save for this game yet") },
        text = {
            Column {
                Text("Start a fresh game, or import a .sav file (for example from a PC emulator)?")
                if (error != null) {
                    Spacer(Modifier.height(12.dp))
                    Text(error, color = MaterialTheme.colorScheme.error)
                }
            }
        },
        confirmButton = { TextButton(onClick = onImport) { Text("Import a .sav") } },
        dismissButton = { TextButton(onClick = onStartFresh) { Text("Start fresh") } },
    )
}

/** Which page of the in-game menu is open. */
enum class MenuPage { MAIN, SAVE, LOAD, CHEATS }

/** A dimmed layer over the paused game (it takes every touch) with a card in the middle. */
@Composable
private fun MenuLayer(maxWidth: androidx.compose.ui.unit.Dp = 440.dp, content: @Composable () -> Unit) {
    Box(
        Modifier.fillMaxSize().background(Scrim).pointerInput(Unit) { detectTapGestures { } }.systemBarsPadding(),
        contentAlignment = Alignment.Center,
    ) {
        Surface(shape = MaterialTheme.shapes.large, modifier = Modifier.padding(16.dp).widthIn(max = maxWidth)) {
            content()
        }
    }
}

/**
 * Back while playing: the game is paused behind this menu. [fastSpeed]: 2, 4, 8 or NativeBridge.FF_MAX. Sideways (a
 * short, wide screen) it has two columns, so nothing is below the fold.
 */
@Composable
fun GameMenu(title: String, fastSpeed: Int, onFastSpeed: (Int) -> Unit, onResume: () -> Unit, onSave: () -> Unit,
             onLoad: () -> Unit, onCheats: () -> Unit, onSettings: () -> Unit, onRestart: () -> Unit, onQuit: () -> Unit) {
    BoxWithConstraints {
        val wide = maxWidth > maxHeight && maxHeight < 520.dp
        MenuLayer(maxWidth = if (wide) 720.dp else 440.dp) {
            if (wide) {
                Row(Modifier.padding(16.dp), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    Column(Modifier.weight(1f)) {
                        MenuTitle(title)
                        ResumeButton(onResume)
                        SpeedChoices(fastSpeed, onFastSpeed)
                        RestartQuit(onRestart, onQuit)
                    }
                    Column(Modifier.weight(1f).align(Alignment.CenterVertically)) { MenuTiles(onSave, onLoad, onCheats, onSettings) }
                }
            } else {
                Column(Modifier.padding(16.dp).widthIn(min = 280.dp, max = 360.dp).verticalScroll(rememberScrollState())) {
                    MenuTitle(title)
                    ResumeButton(onResume)
                    Spacer(Modifier.height(10.dp))
                    MenuTiles(onSave, onLoad, onCheats, onSettings)
                    SpeedChoices(fastSpeed, onFastSpeed)
                    RestartQuit(onRestart, onQuit)
                }
            }
        }
    }
}

@Composable
private fun MenuTitle(title: String) {
    Text("Paused", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
    Text(title, style = MaterialTheme.typography.titleLarge, maxLines = 2, overflow = TextOverflow.Ellipsis)
    Spacer(Modifier.height(14.dp))
}

@Composable
private fun ResumeButton(onResume: () -> Unit) {
    Button(onClick = onResume, modifier = Modifier.fillMaxWidth().heightIn(min = 52.dp)) {
        Icon(Icons.Filled.PlayArrow, contentDescription = null)
        Spacer(Modifier.width(8.dp))
        Text("Resume", style = MaterialTheme.typography.titleMedium)
    }
}

@Composable
private fun MenuTiles(onSave: () -> Unit, onLoad: () -> Unit, onCheats: () -> Unit, onSettings: () -> Unit) {
    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        MenuTile(Icons.Filled.Save, "Save state", onSave, Modifier.weight(1f))
        MenuTile(Icons.Filled.FolderOpen, "Load state", onLoad, Modifier.weight(1f))
    }
    Spacer(Modifier.height(10.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        MenuTile(Icons.Filled.AutoFixHigh, "Cheats", onCheats, Modifier.weight(1f))
        MenuTile(Icons.Filled.Settings, "Settings", onSettings, Modifier.weight(1f))
    }
}

@Composable
private fun SpeedChoices(fastSpeed: Int, onFastSpeed: (Int) -> Unit) {
    Spacer(Modifier.height(14.dp))
    Text("Fast-forward speed", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
    Spacer(Modifier.height(6.dp))
    Choices(Settings.SPEEDS.map(Settings::speedName), Settings.SPEEDS.indexOf(fastSpeed).coerceAtLeast(0),
        onPick = { onFastSpeed(Settings.SPEEDS[it]) })
}

@Composable
private fun RestartQuit(onRestart: () -> Unit, onQuit: () -> Unit) {
    Spacer(Modifier.height(16.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        FilledTonalButton(onClick = onRestart, modifier = Modifier.weight(1f).heightIn(min = 48.dp),
            colors = ButtonDefaults.filledTonalButtonColors(containerColor = MaterialTheme.colorScheme.surfaceContainer,
                contentColor = MaterialTheme.colorScheme.onSurface)) {
            Icon(Icons.Filled.RestartAlt, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(6.dp))
            Text("Restart")
        }
        FilledTonalButton(onClick = onQuit, modifier = Modifier.weight(1f).heightIn(min = 48.dp),
            colors = ButtonDefaults.filledTonalButtonColors(containerColor = MaterialTheme.colorScheme.errorContainer,
                contentColor = MaterialTheme.colorScheme.error)) {
            Icon(Icons.AutoMirrored.Filled.ExitToApp, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(6.dp))
            Text("Quit")
        }
    }
}

/**
 * The 10 slots as a grid of pictures (two columns). Saving: every slot can be picked; loading: only used ones.
 * [refresh] changes whenever the slots may have changed, so the pictures are read again.
 */
@Composable
fun SlotGrid(saving: Boolean, states: States, refresh: Int, onPick: (Int) -> Unit, onBack: () -> Unit) {
    val pictures = remember(refresh) { (1..States.SLOTS).map { if (states.used(it)) states.loadPicture(it) else null } }
    val used = remember(refresh) { (1..States.SLOTS).map { states.used(it) } }
    val dates = remember(refresh) {
        val format = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
        (1..States.SLOTS).map { if (states.used(it)) format.format(Date(states.savedAt(it))) else null }
    }
    val slots = (1..States.SLOTS).filter { saving || used[it - 1] }
    MenuLayer {
        Column(Modifier.padding(bottom = 12.dp)) {
            ScreenHeader(if (saving) "Save state" else "Load state", onBack = onBack,
                subtitle = if (saving) "Pick a slot" else null)
            Column(Modifier.padding(horizontal = 12.dp).verticalScroll(rememberScrollState())) {
                if (slots.isEmpty()) {
                    Text("No saved states yet. Use Save state first.", color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(12.dp))
                }
                for (pair in slots.chunked(2)) {
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.padding(vertical = 5.dp)) {
                        for (slot in pair) SlotCard(slot, pictures[slot - 1], dates[slot - 1], onPick, Modifier.weight(1f))
                        if (pair.size == 1) Spacer(Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

@Composable
private fun SlotCard(slot: Int, picture: Bitmap?, date: String?, onPick: (Int) -> Unit, modifier: Modifier) {
    val outline = MaterialTheme.colorScheme.outline
    Surface(onClick = { onPick(slot) }, shape = MaterialTheme.shapes.medium,
        color = MaterialTheme.colorScheme.surfaceContainer, contentColor = MaterialTheme.colorScheme.onSurface, modifier = modifier) {
        Column(Modifier.padding(6.dp)) {
            val frame = Modifier.fillMaxWidth().aspectRatio(1.5f).clip(RoundedCornerShape(8.dp))
            if (picture != null) {
                Image(picture.asImageBitmap(), contentDescription = "slot $slot", modifier = frame,
                    contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.None)
            } else {
                Box(frame.drawBehind {
                    drawRoundRect(outline, cornerRadius = CornerRadius(8.dp.toPx()),
                        style = Stroke(width = 1.5.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(10f, 8f))))
                }, contentAlignment = Alignment.Center) {
                    Text("Empty", color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
                }
            }
            Text("Slot $slot", style = MaterialTheme.typography.labelLarge, modifier = Modifier.padding(start = 2.dp, top = 6.dp))
            Text(date ?: " ", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.padding(start = 2.dp))
        }
    }
}

/** The type picker's names, by NativeBridge.CHEAT_* value. */
val CHEAT_TYPES = listOf("Auto", "Action Replay v3", "GameShark v1/v2", "CodeBreaker", "address:value")

/** The game's cheats: one row each (name, on/off switch); a tap on the name edits it. */
@Composable
fun CheatPanel(title: String, cheats: List<Cheat>, onToggle: (Int, Boolean) -> Unit, onEdit: (Int) -> Unit,
               onAdd: () -> Unit, onBack: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(bottom = 16.dp).widthIn(min = 300.dp)) {
            ScreenHeader("Cheats", onBack = onBack, subtitle = title)
            Column(Modifier.padding(horizontal = 16.dp)) {
                Column(Modifier.heightIn(max = 360.dp).verticalScroll(rememberScrollState())) {
                    if (cheats.isEmpty()) {
                        Text("No cheats yet. Add Action Replay, GameShark or CodeBreaker codes.",
                            color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(vertical = 8.dp))
                    }
                    cheats.forEachIndexed { i, c ->
                        Surface(shape = MaterialTheme.shapes.medium, color = MaterialTheme.colorScheme.surfaceContainer,
                            contentColor = MaterialTheme.colorScheme.onSurface, modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(c.name, Modifier.weight(1f).clickable { onEdit(i) }.padding(14.dp),
                                    maxLines = 2, overflow = TextOverflow.Ellipsis)
                                Switch(checked = c.on, onCheckedChange = { onToggle(i, it) }, modifier = Modifier.padding(end = 10.dp))
                            }
                        }
                    }
                }
                Spacer(Modifier.height(10.dp))
                Button(onClick = onAdd, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Icon(Icons.Filled.Add, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Add cheat")
                }
            }
        }
    }
}

/**
 * Adding (editing == null) or editing a cheat: name, codes (one per line), type. [check] reads the codes:
 * [the type found, the first bad line (0 = none)]. Save refuses unreadable codes and names the line.
 */
@Composable
fun CheatDialog(editing: Cheat?, check: (String, Int) -> IntArray, onSave: (Cheat) -> Unit, onDelete: (() -> Unit)?,
                onCancel: () -> Unit) {
    var name by remember { mutableStateOf(editing?.name ?: "") }
    var codes by remember { mutableStateOf(editing?.codes ?: "") }
    var type by remember { mutableStateOf(editing?.type ?: NativeBridge.CHEAT_AUTO) }
    var picking by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val found = remember(codes, type) { if (codes.isBlank()) null else check(codes, type) }
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text(if (editing == null) "Add cheat" else "Edit cheat") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                OutlinedTextField(value = name, onValueChange = { name = it }, label = { Text("Name") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(value = codes, onValueChange = { codes = it; error = null },
                    label = { Text("Codes (one per line)") }, minLines = 4, maxLines = 10,
                    textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
                    modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
                Box {
                    OutlinedButton(onClick = { picking = true }) { Text("Type: ${CHEAT_TYPES[type]}") }
                    DropdownMenu(expanded = picking, onDismissRequest = { picking = false }) {
                        CHEAT_TYPES.forEachIndexed { i, t ->
                            DropdownMenuItem(text = { Text(t) }, onClick = { type = i; picking = false; error = null })
                        }
                    }
                }
                val hint = when {
                    error != null -> error
                    found == null -> null
                    found[1] != 0 -> "Line ${found[1]} isn't a code this app can read."
                    type == NativeBridge.CHEAT_AUTO -> "Found: ${CHEAT_TYPES[found[0]]}"
                    else -> null
                }
                if (hint != null) {
                    Text(hint, color = if (found != null && found[1] == 0 && error == null) MaterialTheme.colorScheme.onSurfaceVariant
                        else MaterialTheme.colorScheme.error, modifier = Modifier.padding(top = 6.dp))
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val result = check(codes, type)
                if (codes.isBlank()) error = "Type or paste the codes first."
                else if (result[1] != 0) error = "Line ${result[1]} isn't a code this app can read."
                else {
                    val shown = name.trim().ifEmpty { codes.lineSequence().first { it.isNotBlank() }.trim() }
                    onSave(Cheat(shown, type, codes, editing?.on ?: true))
                }
            }) { Text("Save") }
        },
        dismissButton = {
            Row {
                if (onDelete != null) DangerButton("Delete", onDelete)
                TextButton(onClick = onCancel) { Text("Cancel") }
            }
        },
    )
}

@Composable
fun DeleteCheatDialog(name: String, onDelete: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Delete $name?") },
        text = { Text("The cheat and its codes are removed from this game.") },
        confirmButton = { DangerButton("Delete", onDelete) },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun ReplaceDialog(slot: Int, onReplace: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Replace slot $slot?") },
        text = { Text("The state saved there is overwritten.") },
        confirmButton = { DangerButton("Replace", onReplace) },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun RestartDialog(onRestart: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Restart from the title screen?") },
        text = { Text("The current moment is lost unless it's saved in a slot.") },
        confirmButton = { DangerButton("Restart", onRestart) },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/**
 * The game (a SurfaceView handed to the native render thread) and the touch buttons, in layout A; for a solar
 * cartridge also the sun button with the light slider ([manualLight] null = Auto, the phone's light sensor).
 */
@Composable
fun GameScreen(controlsHidden: Boolean, fastOn: Boolean, onTouchMask: (Int) -> Unit, onShowControls: () -> Unit,
               onDrawFailed: () -> Unit, solar: Boolean = false, manualLight: Int? = null,
               onManualLight: (Int?) -> Unit = {}, touchLook: Int = Settings.LOOK_NORMAL, buzz: Boolean = true,
               layouts: LayoutStore.Layouts = LayoutStore.Layouts(), showSpeed: Boolean = false, skin: Skin = Skins.PLAIN,
               fitMode: Int = Settings.SIZE_FIT) {
    // The skin's body round the picture: here, and inside the game's surface by OpenGL (NativeBridge.setBackground).
    BoxWithConstraints(Modifier.fillMaxSize().background(Color(skin.body))) {
        val sideways = maxWidth > maxHeight
        val edits = layouts.of(sideways)
        // The game's surface in the layout's picture box (upright by default: full width at the top; sideways: the whole
        // screen, the game fitted inside); the buttons over the whole screen.
        val density = LocalDensity.current
        val pic = TouchLayout(constraints.maxWidth.toFloat(), constraints.maxHeight.toFloat(), density.density, sideways, edits).picture
        AndroidView(
            modifier = Modifier.offset { IntOffset(pic.left.roundToInt(), pic.top.roundToInt()) }
                .requiredSize(with(density) { (pic.right - pic.left).toDp() }, with(density) { (pic.bottom - pic.top).toDp() }),
            factory = { gameSurface(it, onDrawFailed) },
        )
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { TouchControlsView(it).also { onTouchMask(0) } }, // a new view (e.g. after turning): nothing held
            update = { v ->
                v.sideways = sideways
                v.edits = edits
                v.hidden = controlsHidden
                v.fastOn = fastOn
                v.look = touchLook
                v.skin = skin
                v.fitMode = fitMode
                v.buzz = buzz
                v.onMask = onTouchMask
                v.onShow = onShowControls
            },
        )
        if (solar) SunControl(manualLight, onManualLight, Modifier.align(Alignment.BottomStart))
        if (showSpeed) SpeedLabel(Modifier.align(Alignment.TopCenter))
    }
}

/** Show speed: "60 fps · 1.0×", measured from the frames the emulator ran, changing once a second. */
@Composable
private fun SpeedLabel(modifier: Modifier) {
    var label by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(Unit) {
        val meter = SpeedMeter()
        while (true) {
            label = meter.sample(System.nanoTime(), NativeBridge.debugFrameCount())
            delay(250)
        }
    }
    label?.let {
        Text(it, color = MaterialTheme.colorScheme.onSurface, style = MaterialTheme.typography.labelMedium,
            modifier = modifier.padding(top = 4.dp).background(Scrim, RoundedCornerShape(8.dp))
                .padding(horizontal = 8.dp, vertical = 3.dp))
    }
}

/** The sun button: opens a light slider (moving it sets the level by hand) with an Auto switch (the phone's light sensor). */
@Composable
private fun SunControl(level: Int?, onLevel: (Int?) -> Unit, modifier: Modifier) {
    var open by remember { mutableStateOf(false) }
    Column(modifier.padding(8.dp)) {
        if (open) {
            Surface(shape = MaterialTheme.shapes.medium, color = MaterialTheme.colorScheme.surfaceContainer.copy(alpha = 0.94f)) {
                Column(Modifier.padding(12.dp).width(260.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Light: ${level ?: "sensor"}", modifier = Modifier.weight(1f))
                        Text("Auto", modifier = Modifier.padding(end = 8.dp))
                        Switch(checked = level == null, onCheckedChange = { onLevel(if (it) null else 128) })
                    }
                    Slider(value = (level ?: 128).toFloat(), onValueChange = { onLevel(it.roundToInt()) },
                        valueRange = 0f..255f)
                }
            }
            Spacer(Modifier.height(4.dp))
        }
        FilledTonalIconButton(onClick = { open = !open }, modifier = Modifier.size(52.dp),
            colors = IconButtonDefaults.filledTonalIconButtonColors(containerColor = Scrim, contentColor = MaterialTheme.colorScheme.primary)) {
            Icon(Icons.Filled.WbSunny, contentDescription = "Light", modifier = Modifier.size(28.dp))
        }
    }
}

private fun gameSurface(context: Context, onDrawFailed: () -> Unit) = SurfaceView(context).apply {
    holder.addCallback(object : SurfaceHolder.Callback {
        override fun surfaceCreated(holder: SurfaceHolder) {
            if (!NativeBridge.setSurface(holder.surface)) onDrawFailed()
        }

        override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            if (!NativeBridge.setSurface(holder.surface)) onDrawFailed()
        }

        override fun surfaceDestroyed(holder: SurfaceHolder) {
            NativeBridge.setSurface(null)
        }
    })
}
```

Replace `app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt`:

```kotlin
package com.tengen.gbaemu

import android.graphics.Bitmap
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import kotlin.math.roundToInt

/**
 * The touch layout editor (Settings → Touch buttons → Edit upright / sideways layout): the buttons and the game picture as in play
 * ([picture]: the paused game's, or a plain block), dragged with one finger; a tap picks one and the bar shows its
 * size slider (½×–2×). Reset all / Cancel / Save; Back = Cancel. The caller turns the screen to [sideways] first.
 */
@Composable
fun LayoutEditor(sideways: Boolean, start: Map<LayoutItem, Place>, picture: Bitmap?, look: Int,
                 onSave: (Map<LayoutItem, Place>) -> Unit, onCancel: () -> Unit, skin: Skin = Skins.PLAIN,
                 fitMode: Int = Settings.SIZE_FIT) {
    var edits by remember { mutableStateOf(start) }
    var selected by remember { mutableStateOf<LayoutItem?>(null) }
    BackHandler { onCancel() }
    BoxWithConstraints(Modifier.fillMaxSize().background(Color(skin.body))) {
        val turnedRight = (maxWidth > maxHeight) == sideways // the screen has turned to the edited orientation
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { TouchControlsView(it).apply { editing = true } },
            update = { v ->
                v.sideways = sideways
                v.edits = edits
                v.look = look
                v.skin = skin
                v.fitMode = fitMode
                v.picture = picture
                v.selected = selected
                v.onSelect = { selected = it }
                v.onEdit = { edits = it }
            },
        )
        if (!turnedRight) return@BoxWithConstraints // a moment while the screen turns
        val layout = TouchLayout(constraints.maxWidth.toFloat(), constraints.maxHeight.toFloat(),
            LocalDensity.current.density, sideways, edits)
        // The bar away from the picked item (nothing picked: at the top, where upright the picture is big enough).
        val atTop = selected?.let { layout.box(it).cy > layout.height / 2 } ?: true
        Surface(
            color = MaterialTheme.colorScheme.surfaceContainerLow.copy(alpha = 0.94f), shape = MaterialTheme.shapes.medium,
            modifier = Modifier.align(if (atTop) Alignment.TopCenter else Alignment.BottomCenter)
                .systemBarsPadding().padding(8.dp).widthIn(max = 480.dp).fillMaxWidth(),
        ) {
            Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp)) {
                val item = selected
                if (item == null) {
                    Text("Drag anything to move it. Tap one to change its size.", fontSize = 13.sp)
                } else {
                    val scale = layout.place(item).scale
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("${item.label} · size ${(scale * 100).roundToInt()}%", fontSize = 13.sp, modifier = Modifier.weight(1f))
                    }
                    Slider(
                        value = scale, valueRange = TouchLayout.MIN_SCALE..TouchLayout.MAX_SCALE,
                        onValueChange = { s ->
                            val sized = layout.with(item, layout.place(item).copy(scale = s))
                            edits = edits + (item to sized.place(item)) // kept inside the screen
                        },
                    )
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TextButton(onClick = { edits = emptyMap(); selected = null }) { Text("Reset all") }
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = onCancel) { Text("Cancel") }
                    Button(onClick = { onSave(edits) }) { Text("Save") }
                }
                Text(if (sideways) "Sideways layout" else "Upright layout", fontSize = 11.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.FastForward
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.SportsEsports
import androidx.compose.material.icons.filled.StayCurrentLandscape
import androidx.compose.material.icons.filled.StayCurrentPortrait
import androidx.compose.material.icons.filled.TouchApp
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp

/** The Settings screen's pages: the sections, then one page each; the Skin page (8b) inside Touch buttons. */
enum class SettingsPage(val title: String) {
    MAIN("Settings"), PICTURE("Picture"), SOUND("Sound"), SPEED("Fast-forward and rewind"), KEYS("Keyboard and controller"),
    TOUCH("Touch buttons"), BIOS("BIOS"), SKIN("Skin");

    /** Where Back goes: the section a page is in; from the main page out of Settings (null). */
    val up: SettingsPage? get() = when (this) {
        MAIN -> null
        SKIN -> TOUCH
        else -> MAIN
    }
}

/** A pick list waiting to be shown: its title, the choices, the one chosen now, and what picking does. */
private class Choice(val title: String, val options: List<String>, val selected: Int, val onPick: (Int) -> Unit)

/** The choice buttons' short labels, where the full names (the main page's summaries) don't fit a phone-width row. */
val SIZE_SHORT = listOf("Fit", "Whole steps", "Stretch")
val LENGTH_SHORT = listOf("1 min", "2½ min", "5 min")

/**
 * Settings (from the library's gear and the in-game menu). [keysChanged] changes whenever the key bindings do, so
 * the key table is read again. Every change goes to [onChange] at once (saved and applied by the caller).
 */
@Composable
fun SettingsScreen(page: SettingsPage, settings: Settings, keys: KeyBindings, keysChanged: Int, biosName: String,
                   onPage: (SettingsPage) -> Unit, onBack: () -> Unit, onChange: (Settings) -> Unit,
                   onPickKey: (bit: Int, device: Int) -> Unit, onResetKeys: () -> Unit, onChooseBios: () -> Unit,
                   onBuiltInBios: () -> Unit, onEditLayout: (sideways: Boolean) -> Unit = {}) {
    val s = settings
    Surface(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().systemBarsPadding()) {
            ScreenHeader(page.title, onBack = onBack)
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp)
                .padding(bottom = 24.dp).widthIn(max = 640.dp)) {
                when (page) {
                    SettingsPage.MAIN -> {
                        NavRow(Icons.Filled.Image, "Picture", "${if (s.smooth) "Smooth" else "Sharp"} · ${Settings.SIZE_NAMES[s.size]}") { onPage(SettingsPage.PICTURE) }
                        NavRow(Icons.AutoMirrored.Filled.VolumeUp, "Sound", if (s.sound) "On · ${Settings.DELAY_NAMES[s.delay]} delay" else "Off") { onPage(SettingsPage.SOUND) }
                        NavRow(Icons.Filled.FastForward, "Fast-forward and rewind", Settings.speedName(s.fastSpeed) + " · " +
                            if (s.rewind) "Rewind ${Settings.LENGTH_NAMES[s.rewindLength].lowercase()}" else "Rewind off") { onPage(SettingsPage.SPEED) }
                        NavRow(Icons.Filled.SportsEsports, "Keyboard and controller", "Which key does what") { onPage(SettingsPage.KEYS) }
                        NavRow(Icons.Filled.TouchApp, "Touch buttons", "${Skins.at(s.skin).name} · ${Settings.LOOK_NAMES[s.touchLook]} · Buzz ${if (s.buzz) "on" else "off"}") { onPage(SettingsPage.TOUCH) }
                        NavRow(Icons.Filled.Memory, "BIOS", biosName) { onPage(SettingsPage.BIOS) }
                    }
                    SettingsPage.PICTURE -> {
                        ChoiceRow("Pixels", listOf("Sharp", "Smooth"), if (s.smooth) 1 else 0, { onChange(s.copy(smooth = it == 1)) },
                            help = "Sharp keeps every GBA pixel crisp; Smooth softens the edges.")
                        ChoiceRow("Size", SIZE_SHORT, s.size, { onChange(s.copy(size = it)) },
                            help = "Whole steps: every GBA pixel the same size (crisper, a little smaller). Sideways, the size matters most.")
                        SwitchRow("Show speed", s.showSpeed, { onChange(s.copy(showSpeed = it)) },
                            help = "Frames per second at the top of the game, and how many times real speed (1.0× = normal).")
                    }
                    SettingsPage.SOUND -> {
                        SwitchRow("Sound", s.sound, { onChange(s.copy(sound = it)) })
                        ChoiceRow("Delay", Settings.DELAY_NAMES, s.delay, { onChange(s.copy(delay = it)) },
                            help = "Low is the quickest. If the sound crackles on a busy phone, try Medium or High.")
                    }
                    SettingsPage.SPEED -> {
                        ChoiceRow("Fast-forward speed", Settings.SPEEDS.map(Settings::speedName),
                            Settings.SPEEDS.indexOf(s.fastSpeed).coerceAtLeast(0), { onChange(s.copy(fastSpeed = Settings.SPEEDS[it])) },
                            help = "Max runs as fast as the phone can, without sound.")
                        SwitchRow("Rewind", s.rewind, { onChange(s.copy(rewind = it)) })
                        ChoiceRow("Rewind length", LENGTH_SHORT, s.rewindLength, { onChange(s.copy(rewindLength = it)) },
                            help = "Uses up to 24, 64 or 128 MB. Rewind changes are used the next time a game opens.")
                    }
                    SettingsPage.KEYS -> KeyTable(keys, keysChanged, onPickKey, onResetKeys)
                    SettingsPage.TOUCH -> {
                        NavRow(Icons.Filled.Palette, "Skin", Skins.at(s.skin).name) { onPage(SettingsPage.SKIN) }
                        ChoiceRow("Sideways, over the picture", Settings.LOOK_NAMES, s.touchLook, { onChange(s.copy(touchLook = it)) },
                            help = "How strongly the buttons show over the game when the phone is sideways.")
                        SwitchRow("Buzz", s.buzz, { onChange(s.copy(buzz = it)) },
                            help = "Follows the phone's own touch-vibration setting.")
                        Spacer(Modifier.height(8.dp))
                        NavRow(Icons.Filled.StayCurrentPortrait, "Edit upright layout", "Move and size every button and the game picture") { onEditLayout(false) }
                        NavRow(Icons.Filled.StayCurrentLandscape, "Edit sideways layout", "Move and size every button and the game picture") { onEditLayout(true) }
                    }
                    SettingsPage.SKIN -> SkinGrid(s.skin) { onChange(s.copy(skin = it)) }
                    SettingsPage.BIOS -> {
                        Row(Modifier.fillMaxWidth().padding(vertical = 14.dp), verticalAlignment = Alignment.CenterVertically) {
                            Text("In use", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                            Text(biosName, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        Button(onClick = onChooseBios, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("Choose BIOS file") }
                        if (s.biosFile) {
                            Spacer(Modifier.height(8.dp))
                            OutlinedButton(onClick = onBuiltInBios, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("Use built-in BIOS") }
                        }
                        Help("A dump of your own GBA's BIOS (16 KB). Games then start with the Nintendo logo. Used the next time a game opens; save states made with the other BIOS can't be loaded.")
                    }
                }
            }
        }
    }
}

/**
 * The Skin page (8b): every skin as a small phone in its colours, its name under it; the chosen one with an amber
 * border. A tap picks it at once. Rows of tiles (the page already scrolls): 3 across upright, more on a wide screen.
 */
@Composable
private fun SkinGrid(selected: Int, onPick: (Int) -> Unit) {
    BoxWithConstraints(Modifier.fillMaxWidth().padding(top = 8.dp)) {
        val across = (maxWidth / 112.dp).toInt().coerceAtLeast(3)
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Skins.ALL.indices.chunked(across).forEach { row ->
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    for (i in row) SkinTile(Skins.ALL[i], i == selected, { onPick(i) }, Modifier.weight(1f))
                    repeat(across - row.size) { Spacer(Modifier.weight(1f)) }
                }
            }
        }
    }
}

@Composable
private fun SkinTile(skin: Skin, chosen: Boolean, onClick: () -> Unit, modifier: Modifier) {
    Surface(onClick = onClick, shape = MaterialTheme.shapes.medium, color = MaterialTheme.colorScheme.surfaceContainer,
        border = if (chosen) BorderStroke(2.dp, MaterialTheme.colorScheme.primary) else null, modifier = modifier) {
        Column(Modifier.padding(8.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Canvas(Modifier.fillMaxWidth().aspectRatio(0.5f).clip(RoundedCornerShape(8.dp))) { drawSkinPhone(skin) }
            Text(skin.name, style = MaterialTheme.typography.labelLarge, maxLines = 1, modifier = Modifier.padding(top = 6.dp))
        }
    }
}

/** A phone upright in [skin]'s colours: the body, a picture block at the top, the D-pad, A/B, L/R and Select/Start. */
private fun DrawScope.drawSkinPhone(skin: Skin) {
    val w = size.width
    val h = size.height
    drawRect(Color(skin.body)) // the tile clips the corners round
    val pic = w * 2 / 3
    drawRect(Color(0xFF7EC8F0), size = Size(w, pic * 0.55f))
    drawRect(Color(0xFF5AA04A), topLeft = Offset(0f, pic * 0.55f), size = Size(w, pic * 0.45f))
    val rest = h - pic // the controls, placed as in the upright layout (fractions of 412 x 640 dp)
    fun y(f: Float) = pic + rest * f
    val rim = skin.rim?.let { Color(it) }
    val stroke = Stroke(width = w * 0.012f)
    fun pill(c: Int, l: Float, t: Float, r: Float, b: Float) {
        val tl = Offset(w * l, y(t))
        val sz = Size(w * (r - l), y(b) - y(t))
        val cr = CornerRadius(sz.height / 2)
        drawRoundRect(Color(c), tl, sz, cr)
        if (rim != null) drawRoundRect(rim, tl, sz, cr, style = stroke)
    }
    fun dot(c: Int, x: Float, yf: Float, r: Float) {
        drawCircle(Color(c), w * r, Offset(w * x, y(yf)))
        if (rim != null) drawCircle(rim, w * r, Offset(w * x, y(yf)), style = stroke)
    }
    pill(skin.shoulder, 0f, 0.025f, 0.233f, 0.088f)
    pill(skin.shoulder, 0.767f, 0.025f, 1f, 0.088f)
    val dx = w * 0.209f
    val dy = y(0.506f)
    val r = w * 0.17f
    val a = r * 0.36f
    for ((tl, sz) in listOf(Offset(dx - r, dy - a) to Size(2 * r, 2 * a), Offset(dx - a, dy - r) to Size(2 * a, 2 * r))) {
        drawRoundRect(Color(skin.dpad), tl, sz, CornerRadius(a / 2))
    }
    dot(skin.ab, 0.874f, 0.463f, 0.087f)
    dot(skin.ab, 0.670f, 0.550f, 0.087f)
    pill(skin.small, 0.286f, 0.925f, 0.481f, 0.975f)
    pill(skin.small, 0.519f, 0.925f, 0.714f, 0.975f)
}

/** A setting's current value in a row (the game settings' pick lists). */
@Composable
private fun ValueRow(label: String, value: String, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clickable(onClick = onClick).heightIn(min = 48.dp).padding(vertical = 10.dp),
        horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = MaterialTheme.typography.bodyLarge)
        Text(value, color = MaterialTheme.colorScheme.primary)
    }
    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
}

/** A pick list: tapping a choice picks it. */
@Composable
fun ChoiceDialog(title: String, options: List<String>, selected: Int, onPick: (Int) -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text(title) },
        text = {
            Column {
                options.forEachIndexed { i, label ->
                    Row(Modifier.fillMaxWidth().selectable(selected = i == selected, onClick = { onPick(i) }).heightIn(min = 48.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = i == selected, onClick = null)
                        Text(label, modifier = Modifier.padding(start = 8.dp))
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** Each GBA button (and fast-forward, rewind) with its keyboard key and controller button; a tap waits for the new one. */
@Composable
private fun KeyTable(keys: KeyBindings, keysChanged: Int, onPickKey: (Int, Int) -> Unit, onReset: () -> Unit) {
    val rows = remember(keysChanged) {
        KeyMap.bindings.map { b ->
            Triple(b, KeyBindings.keyName(keys.key(b.bit, KeyBindings.KEYBOARD)), KeyBindings.keyName(keys.key(b.bit, KeyBindings.CONTROLLER)))
        }
    }
    Help("Tap a key, then press the new one. A key already used moves here; its old button shows \"—\". The phone's touch buttons stay as they are.")
    Row(Modifier.fillMaxWidth().padding(top = 12.dp, bottom = 4.dp)) {
        for (h in listOf("GBA", "Keyboard", "Controller")) {
            Text(h, Modifier.weight(1f), style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
    for ((b, kbd, pad) in rows) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(b.button, Modifier.weight(1f))
            TextButton(onClick = { onPickKey(b.bit, KeyBindings.KEYBOARD) }, Modifier.weight(1f)) { Text(kbd) }
            TextButton(onClick = { onPickKey(b.bit, KeyBindings.CONTROLLER) }, Modifier.weight(1f)) { Text(pad) }
        }
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
    }
    Spacer(Modifier.height(16.dp))
    OutlinedButton(onClick = onReset, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("Reset to defaults") }
}

/**
 * Waiting for the key for one GBA button; Back cancels (Back itself can't be a game key). A layer on the same window,
 * not a dialog: a dialog's window would get the key instead of MainActivity.dispatchKeyEvent.
 */
@Composable
fun KeyWaitLayer(button: String, device: Int, onCancel: () -> Unit) {
    Box(Modifier.fillMaxSize().background(Scrim).pointerInput(Unit) { detectTapGestures { } },
        contentAlignment = Alignment.Center) {
        Surface(shape = MaterialTheme.shapes.large, color = MaterialTheme.colorScheme.surfaceContainerLow, modifier = Modifier.padding(24.dp)) {
            Column(Modifier.padding(20.dp)) {
                Text("$button · ${if (device == KeyBindings.KEYBOARD) "keyboard" else "controller"}",
                    style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(12.dp))
                Text("Press the ${if (device == KeyBindings.KEYBOARD) "key" else "controller button"} for $button",
                    textAlign = TextAlign.Start)
                TextButton(onClick = onCancel, modifier = Modifier.align(Alignment.End)) { Text("Cancel") }
            }
        }
    }
}

/** A 16 KB file that isn't the usual GBA BIOS. */
@Composable
fun UnusualBiosDialog(onUse: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Not the usual GBA BIOS") },
        text = { Text("This isn't the usual GBA BIOS; games may not start. Use it anyway?") },
        confirmButton = { TextButton(onClick = onUse) { Text("Use it") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** Game settings (a game's sheet → Game settings): save type and clock, Auto showing what was detected. */
@Composable
fun GameSettingsDialog(name: String, game: GameSettings.Game, detected: Pair<Int, Boolean>?, onChange: (GameSettings.Game) -> Unit,
                       onDone: () -> Unit) {
    var choosing by remember { mutableStateOf<Choice?>(null) }
    val autoSave = "Auto" + (detected?.let { " (${GameSettings.saveName(it.first)})" } ?: "")
    val autoClock = "Auto" + (detected?.let { if (it.second) " (has one)" else " (none)" } ?: "")
    val saveOptions = listOf(autoSave) + GameSettings.SAVE_CHOICES.map(GameSettings::saveName)
    val saveIndex = if (game.saveType == GameSettings.AUTO) 0 else GameSettings.SAVE_CHOICES.indexOf(game.saveType) + 1
    val clockIndex = when (game.clock) {
        GameSettings.CLOCK_ON -> 1
        GameSettings.CLOCK_OFF -> 2
        else -> 0
    }
    AlertDialog(
        onDismissRequest = onDone,
        title = { Text("Game settings") },
        text = {
            Column {
                Text(name, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(bottom = 8.dp))
                ValueRow("Save type", saveOptions[saveIndex]) {
                    choosing = Choice("Save type", saveOptions, saveIndex) {
                        onChange(game.copy(saveType = if (it == 0) GameSettings.AUTO else GameSettings.SAVE_CHOICES[it - 1]))
                    }
                }
                Text("Clock", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.padding(top = 12.dp, bottom = 8.dp))
                Choices(listOf(autoClock, "On", "Off"), clockIndex, onPick = {
                    onChange(game.copy(clock = when (it) {
                        1 -> GameSettings.CLOCK_ON
                        2 -> GameSettings.CLOCK_OFF
                        else -> GameSettings.AUTO
                    }))
                })
                Help("Used the next time this game opens. The save file is kept either way; it only loads if it fits the save type.")
            }
        },
        confirmButton = { TextButton(onClick = onDone) { Text("Done") } },
    )
    choosing?.let { c -> ChoiceDialog(c.title, c.options, c.selected, onPick = { c.onPick(it); choosing = null }, onCancel = { choosing = null }) }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.OpenableColumns
import android.view.KeyEvent
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.io.File
import java.nio.ByteBuffer
import kotlin.concurrent.thread
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private var playing by mutableStateOf(false)
    private var message by mutableStateOf<String?>(null)
    private lateinit var library: Library
    private var rows by mutableStateOf(listOf<GameEntry>()) // the list as shown
    private var hasFolder by mutableStateOf(false) // library.folder is set (the screen watches this, not the Library)
    private var folderName by mutableStateOf<String?>(null)
    private var folderLost by mutableStateOf(false)
    private var scanning by mutableStateOf(false)
    private var scanRun = 0 // only the newest scan's result is used
    private var libraryRefresh by mutableStateOf(0) // re-reads the list's pictures
    private var holdGame by mutableStateOf<GameEntry?>(null)
    private var replaceSave by mutableStateOf<Pair<GameEntry, ByteArray>?>(null)
    private var saveGame: GameEntry? = null // the game whose save is being exported / imported
    private var exportBytes: ByteArray? = null
    private lateinit var settingsStore: IntStore
    private var settings by mutableStateOf(Settings())
    private var settingsPage by mutableStateOf<SettingsPage?>(null) // the Settings screen, or null
    private lateinit var keys: KeyBindings
    private var keysChanged by mutableStateOf(0) // re-reads the key table
    private var waitingKey by mutableStateOf<Pair<Int, Int>?>(null) // (button bit, KeyBindings device) being remapped
    private var unusualBios by mutableStateOf<ByteArray?>(null) // a 16 KB file that isn't the usual BIOS, to confirm
    private lateinit var gameSettings: GameSettings
    private lateinit var layoutStore: LayoutStore
    private var layouts by mutableStateOf(LayoutStore.Layouts()) // the user's touch layouts (6c)
    private var editLayout by mutableStateOf<Boolean?>(null) // the layout editor: sideways, upright, or closed
    private var editPicture: Bitmap? = null // the paused game's picture behind the editor
    private var orientationBeforeEditor = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
    private var editGame by mutableStateOf<Triple<String, String, GameSettings.Game>?>(null) // name, id, its settings
    private var askImport by mutableStateOf(false)
    private var importError by mutableStateOf<String?>(null)
    private var menu by mutableStateOf<MenuPage?>(null) // the in-game menu (the game paused) or null
    private var replaceSlot by mutableStateOf<Int?>(null)
    private var askRestart by mutableStateOf(false)
    private var slotsChanged by mutableStateOf(0) // re-reads the slot pictures
    private var session by mutableStateOf(0) // a new game session gets a new game screen (see startGame)
    private var states: States? = null // the running game's save states
    private var cheatStore: Cheats? = null // the running game's cheats file
    private var cheats by mutableStateOf(listOf<Cheat>())
    private var editCheat by mutableStateOf<Int?>(null) // -1 = a new one
    private var deleteCheat by mutableStateOf<Int?>(null)
    private var current: GameEntry? = null // the running game's row
    private var currentTitle = ""
    private var controlsHidden by mutableStateOf(false)
    private var manualLight by mutableStateOf<Int?>(null)
    private var hardware = 0 // the cartridge's extras (NativeBridge.HW_*)
    private var motion: MotionInput? = null
    private var rumble: Rumble? = null
    private var keyboardMask = 0
    private var touchMask = 0
    private val fastSwitch = FastForwardSwitch()
    private val repeatFilter = KeyRepeatFilter() // F / R held in the phone emulator's window
    private val handler = Handler(Looper.getMainLooper())
    private var fastOn by mutableStateOf(false) // ⏩ lit
    private var fastSpeed by mutableStateOf(4) // 2, 4, 8 or NativeBridge.FF_MAX; remembered for all games

    private val openRom = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) openFromPicker(uri)
    }
    private val pickSave = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importFrom(uri)
    }
    private val pickFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) useFolder(uri)
    }
    private val exportTo = registerForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        if (uri != null) writeExport(uri)
    }
    private val pickBios = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) biosPicked(uri)
    }
    private val importFor = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importPicked(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        library = Library(filesDir)
        if (!library.load()) toast("The game list couldn't be read; it was kept aside and is built again from the folder.")
        carryOverLastGame()
        rows = library.rows()
        hasFolder = library.folder != null
        settingsStore = PrefsStore(getSharedPreferences("settings", MODE_PRIVATE))
        settings = Settings.load(settingsStore)
        fastSpeed = settings.fastSpeed
        applySettings(settings)
        keys = KeyBindings(PrefsStore(getSharedPreferences("keys", MODE_PRIVATE)))
        keys.use()
        gameSettings = GameSettings(PrefsStore(getSharedPreferences("game_settings", MODE_PRIVATE)))
        layoutStore = LayoutStore(filesDir)
        layouts = layoutStore.load()
        if (layoutStore.wasDamaged) toast("The touch layout couldn't be read; it was kept aside and the default layout is used.")
        setContent {
            AppTheme {
                if (playing) key(session) {
                    GameScreen(
                        controlsHidden = controlsHidden,
                        fastOn = fastOn,
                        onTouchMask = { touchMask = it; sendKeys() },
                        onShowControls = { controlsHidden = false },
                        onDrawFailed = {
                            stopGame()
                            message = "This device cannot show the game: OpenGL ES 3.0 failed."
                        },
                        solar = hardware and NativeBridge.HW_SOLAR != 0,
                        manualLight = manualLight,
                        onManualLight = {
                            manualLight = it
                            motion?.manualLight = it
                        },
                        touchLook = settings.touchLook,
                        buzz = settings.buzz,
                        layouts = layouts,
                        showSpeed = settings.showSpeed,
                        skin = Skins.at(settings.skin),
                        fitMode = settings.size,
                    )
                    LaunchedEffect(Unit) { // a failed save-file write: tell once per game
                        while (true) {
                            delay(1000)
                            if (NativeBridge.saveError()) {
                                toast("Could not write the save file.")
                                break
                            }
                        }
                    }
                } else if (folderLost || !hasFolder) { // no folder yet, or it can't be opened
                    FirstStartScreen(folderLost, message, onChooseFolder = { pickFolder.launch(null) },
                        onOpenGame = { openRom.launch(arrayOf("*/*")) }, onSettings = { settingsPage = SettingsPage.MAIN })
                } else {
                    LibraryScreen(rows, folderName, scanning, message, libraryRefresh,
                        picture = { g -> if (g.id.isEmpty()) null else States(filesDir, g.id).loadPicture(States.AUTO) },
                        onPlay = { open(it) }, onHold = { holdGame = it },
                        onOpenGame = { openRom.launch(arrayOf("*/*")) }, onChangeFolder = { pickFolder.launch(null) },
                        onSettings = { settingsPage = SettingsPage.MAIN })
                }
                holdGame?.let { g ->
                    GameHoldMenu(g,
                        onStartFresh = { holdGame = null; open(g, resumeAuto = false) },
                        onImport = { holdGame = null; saveGame = g; importFor.launch(arrayOf("*/*")) },
                        onExport = { holdGame = null; exportSave(g) },
                        onGameSettings = {
                            holdGame = null
                            idOf(g)?.let { id -> editGame = Triple(g.name, id, gameSettings.of(id)) }
                        },
                        onRemove = if (g.outside) ({ holdGame = null; removeGame(g) }) else null,
                        onClose = { holdGame = null })
                }
                replaceSave?.let { (g, bytes) ->
                    ReplaceSaveDialog(g.name, onReplace = { replaceSave = null; importSave(g, bytes) },
                        onCancel = { replaceSave = null })
                }
                if (askImport) {
                    ImportDialog(importError,
                        onStartFresh = { askImport = false; startGame() },
                        onImport = { pickSave.launch(arrayOf("*/*")) },
                        onCancel = { askImport = false; NativeBridge.stop() })
                }
                val st = states
                when (menu) {
                    MenuPage.MAIN -> GameMenu(currentTitle, fastSpeed, onFastSpeed = { changeSettings(settings.copy(fastSpeed = it)) },
                        onResume = ::resumeGame, onSave = { menu = MenuPage.SAVE },
                        onLoad = { menu = MenuPage.LOAD }, onCheats = { menu = MenuPage.CHEATS },
                        onSettings = { settingsPage = SettingsPage.MAIN },
                        onRestart = { askRestart = true }, onQuit = ::quitGame)
                    MenuPage.SAVE -> if (st != null) SlotGrid(saving = true, st, slotsChanged,
                        onPick = { if (st.used(it)) replaceSlot = it else saveTo(it) }, onBack = { menu = MenuPage.MAIN })
                    MenuPage.LOAD -> if (st != null) SlotGrid(saving = false, st, slotsChanged,
                        onPick = ::loadFrom, onBack = { menu = MenuPage.MAIN })
                    MenuPage.CHEATS -> CheatPanel(currentTitle, cheats,
                        onToggle = { i, on -> changeCheats(cheats.toMutableList().also { it[i] = it[i].copy(on = on) }) },
                        onEdit = { editCheat = it }, onAdd = { editCheat = -1 }, onBack = { menu = MenuPage.MAIN })
                    null -> {}
                }
                editCheat?.let { i ->
                    CheatDialog(editing = cheats.getOrNull(i), check = NativeBridge::checkCheat,
                        onSave = { c ->
                            changeCheats(cheats.toMutableList().also { if (i in it.indices) it[i] = c else it += c })
                            editCheat = null
                        },
                        onDelete = if (i in cheats.indices) ({ deleteCheat = i }) else null,
                        onCancel = { editCheat = null })
                }
                deleteCheat?.let { i ->
                    DeleteCheatDialog(cheats[i].name,
                        onDelete = {
                            changeCheats(cheats.toMutableList().also { it.removeAt(i) })
                            deleteCheat = null
                            editCheat = null
                        },
                        onCancel = { deleteCheat = null })
                }
                replaceSlot?.let { slot -> ReplaceDialog(slot, onReplace = { saveTo(slot) }, onCancel = { replaceSlot = null }) }
                if (askRestart) RestartDialog(onRestart = ::restartGame, onCancel = { askRestart = false })
                settingsPage?.let { page ->
                    SettingsScreen(page, settings, keys, keysChanged, if (settings.biosFile) "BIOS file" else "Built-in",
                        onPage = { settingsPage = it },
                        onBack = { settingsPage = page.up },
                        onChange = ::changeSettings,
                        onPickKey = { bit, device -> waitingKey = bit to device },
                        onResetKeys = { keys.reset(); keys.use(); keysChanged++ },
                        onChooseBios = { pickBios.launch(arrayOf("*/*")) },
                        onBuiltInBios = {
                            Bios.remove(filesDir)
                            changeSettings(settings.copy(biosFile = false))
                            toast("The built-in BIOS is used from the next game on.")
                        },
                        onEditLayout = ::openLayoutEditor)
                }
                editLayout?.let { side ->
                    LayoutEditor(side, layouts.of(side), editPicture, settings.touchLook,
                        onSave = { edits ->
                            layouts = layouts.with(side, edits)
                            if (!layoutStore.save(layouts)) toast("Couldn't save the layout.")
                            closeLayoutEditor()
                        },
                        onCancel = ::closeLayoutEditor, skin = Skins.at(settings.skin),
                        fitMode = settings.size)
                }
                waitingKey?.let { (bit, device) ->
                    KeyWaitLayer(KeyMap.bindings.first { it.bit == bit }.button, device, onCancel = { waitingKey = null })
                }
                unusualBios?.let { bytes ->
                    UnusualBiosDialog(onUse = { unusualBios = null; installBios(bytes) }, onCancel = { unusualBios = null })
                }
                editGame?.let { (name, id, game) ->
                    GameSettingsDialog(name, game, gameSettings.detected(id),
                        onChange = { gameSettings.set(id, it); editGame = Triple(name, id, it) },
                        onDone = { editGame = null })
                }
                BackHandler(enabled = playing && menu == null && settingsPage == null) { openMenu() }
                BackHandler(enabled = playing && menu != null && settingsPage == null) {
                    if (menu == MenuPage.MAIN) resumeGame() else menu = MenuPage.MAIN
                }
                BackHandler(enabled = settingsPage != null && waitingKey == null && editLayout == null) {
                    settingsPage = settingsPage?.up
                }
            }
        }
    }

    private fun readBytes(uri: Uri): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use { it.readBytes() }
    } catch (e: Exception) {
        null
    }

    /** The file's name as the picker showed it and its size (0 = unknown), or null. */
    private fun fileInfo(uri: Uri): Pair<String, Long>? = try {
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use {
            if (it.moveToFirst() && !it.isNull(0)) it.getString(0) to (if (it.isNull(1)) 0L else it.getLong(1)) else null
        }
    } catch (e: Exception) {
        null
    }

    /** "Open one game": the game's row if the list has it (e.g. it is in the folder), else a new outside one. */
    private fun openFromPicker(uri: Uri) {
        try { /* so the list can read it again after a restart */
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        val key = try {
            FolderScan.keyOf(uri)
        } catch (e: Exception) {
            uri.toString()
        }
        val info = fileInfo(uri)
        val entry = library.games.firstOrNull { it.key == key }
            ?: GameEntry(key, uri.toString(), Library.nameOf(info?.first ?: uri.lastPathSegment ?: "game"), outside = true,
                size = info?.second ?: 0)
        open(entry)
    }

    /** The games folder picked: its permission kept (the old folder's given back), the list read from it. */
    private fun useFolder(tree: Uri) {
        try {
            contentResolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        library.folder?.let { old ->
            if (old != tree.toString()) try {
                contentResolver.releasePersistableUriPermission(Uri.parse(old), Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (e: Exception) {
            }
        }
        library.folder = tree.toString()
        library.save()
        hasFolder = true
        folderLost = false
        message = null
        refreshLibrary()
    }

    /**
     * Reads the folder again (off the main thread) and shows the list. Played games whose id is not known yet (the
     * last game carried over from before the list) are read once to learn it, so their picture shows. Outside games
     * of unknown size get it, so [Library.merge] can match them with the folder's copy by name and size.
     */
    private fun refreshLibrary() {
        rows = library.rows()
        val folder = library.folder?.let(Uri::parse)
        val unknown = library.games.filter { it.lastPlayed > 0 && it.id.isEmpty() }
        val unsized = library.games.filter { it.outside && it.size <= 0 }
        if (folder == null && unknown.isEmpty() && unsized.isEmpty()) return
        val run = ++scanRun
        scanning = true
        thread(name = "library-scan") {
            val found = folder?.let { FolderScan.scan(contentResolver, it) }
            val name = folder?.let { FolderScan.folderName(contentResolver, it) }
            val ids = unknown.mapNotNull { g -> readGame(Uri.parse(g.uri))?.let { g.key to GameId.of(it) } }.toMap()
            val sizes = unsized.mapNotNull { g -> fileInfo(Uri.parse(g.uri))?.second?.takeIf { it > 0 }?.let { g.key to it } }.toMap()
            runOnUiThread {
                if (run != scanRun) return@runOnUiThread
                scanning = false
                for (g in library.games) ids[g.key]?.let { library.played(g.copy(id = it)) }
                for (g in library.games) sizes[g.key]?.let { library.played(g.copy(size = it)) }
                if (folder != null) {
                    folderLost = found == null
                    if (found != null) library.merge(found)
                }
                folderName = name
                library.save()
                rows = library.rows()
                libraryRefresh++
            }
        }
    }

    /** Before the list (5c and earlier): the last game opened becomes the list's first game. */
    private fun carryOverLastGame() {
        val old = getSharedPreferences("last_game", MODE_PRIVATE)
        val u = old.getString("uri", null) ?: return
        val uri = Uri.parse(u)
        val key = try {
            FolderScan.keyOf(uri)
        } catch (e: Exception) {
            u
        }
        if (library.games.none { it.key == key }) {
            val info = fileInfo(uri)
            val name = info?.first?.let(Library::nameOf) ?: old.getString("title", null) ?: "last game"
            library.played(GameEntry(key, u, name, lastPlayed = System.currentTimeMillis(), outside = true, size = info?.second ?: 0))
            library.save()
        }
        old.edit().clear().apply()
    }

    /** The game's bytes (from a .gba or a .zip), or null. */
    private fun readGame(uri: Uri): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use(RomFile::read)
    } catch (e: Exception) {
        null
    }

    /** The game's id, reading the game if the list doesn't know it yet (then remembered); null if it can't be read. */
    private fun idOf(g: GameEntry): String? {
        if (g.id.isNotEmpty()) return g.id
        val bytes = readGame(Uri.parse(g.uri)) ?: run { toast("Can't open ${g.name}."); return null }
        val id = GameId.of(bytes)
        library.played(g.copy(id = id))
        library.save()
        rows = library.rows()
        return id
    }

    private fun removeGame(g: GameEntry) {
        library.remove(g.key)
        library.save()
        try {
            contentResolver.releasePersistableUriPermission(Uri.parse(g.uri), Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: Exception) {
        }
        rows = library.rows()
    }

    private fun exportSave(g: GameEntry) {
        val id = idOf(g) ?: return
        val bytes = SaveFiles.export(filesDir, id) ?: run { toast("No save for this game yet."); return }
        exportBytes = bytes
        exportTo.launch("${g.name}.sav")
    }

    private fun writeExport(uri: Uri) {
        val bytes = exportBytes ?: return
        exportBytes = null
        try {
            contentResolver.openOutputStream(uri, "wt")?.use { it.write(bytes) } ?: error("no stream")
            toast("Save exported.")
        } catch (e: Exception) {
            toast("Couldn't write the save there.")
        }
    }

    private fun importPicked(uri: Uri) {
        val g = saveGame ?: return
        saveGame = null
        val bytes = readBytes(uri) ?: run { toast("Could not read that file."); return }
        if (!SaveFiles.validSize(bytes.size)) {
            toast("That file isn't a save for this game.")
            return
        }
        val id = idOf(g) ?: return
        if (SaveFiles.file(filesDir, id).isFile) replaceSave = g to bytes else importSave(g, bytes)
    }

    private fun importSave(g: GameEntry, bytes: ByteArray) {
        val id = idOf(g) ?: return
        when (SaveFiles.import(filesDir, id, bytes)) {
            SaveFiles.Import.OK -> toast("Save imported. ${g.name} will start from the title screen.")
            SaveFiles.Import.WRONG_SIZE -> toast("That file isn't a save for this game.")
            SaveFiles.Import.FAILED -> toast("Couldn't write the save.")
        }
        libraryRefresh++ // the picture of where it was left is gone
    }

    /** Opens a game; with its automatic state it resumes the exact moment ([resumeAuto] false: from the title). */
    private fun open(entry: GameEntry, resumeAuto: Boolean = true) {
        val uri = Uri.parse(entry.uri)
        val bytes = try {
            contentResolver.openInputStream(uri)?.use(RomFile::read)
        } catch (e: RomFile.NotAGame) {
            message = "Not a GBA game."
            return
        } catch (e: Exception) {
            null
        }
        if (bytes == null) {
            message = "Can't open ${entry.name}."
            return
        }
        val rom = ByteBuffer.allocateDirect(bytes.size).put(bytes)
        rom.flip()
        val saves = File(filesDir, "saves").apply { mkdirs() }
        val gameId = GameId.of(bytes)
        val bios = if (settings.biosFile) {
            Bios.load(filesDir) ?: null.also { toast("The BIOS file is missing; the built-in BIOS is used.") }
        } else null
        NativeBridge.setBios(bios)
        val own = gameSettings.of(gameId)
        NativeBridge.setGameOverrides(own.saveType, own.clock)
        if (NativeBridge.loadRom(rom, File(saves, "$gameId.sav").path) != 0) {
            message = "Not a GBA game."
            return
        }
        if (own == GameSettings.Game()) NativeBridge.cartSetup().let { gameSettings.remember(gameId, it[0], it[1]) }
        hardware = NativeBridge.cartHardware()
        manualLight = null
        val played = entry.copy(id = gameId, lastPlayed = System.currentTimeMillis())
        library.played(played)
        library.save()
        rows = library.rows()
        message = null
        val st = States(filesDir, gameId)
        states = st
        cheatStore = Cheats(filesDir, gameId).also { store ->
            cheats = store.load() ?: emptyList<Cheat>().also {
                toast("This game's cheats file couldn't be read; it was kept aside and the game starts without cheats.")
            }
        }
        current = played
        currentTitle = played.name
        if (resumeAuto && st.used(States.AUTO)) {
            if (st.load(States.AUTO)) {
                startGame()
                return
            }
            toast("Couldn't resume where you left off; starting the game.")
        }
        when (NativeBridge.saveStatus()) {
            NativeBridge.SAVE_EMPTY -> {
                importError = null
                askImport = true
            }
            NativeBridge.SAVE_MISFIT -> {
                startGame()
                toast("The save file for this game doesn't fit it and was not loaded.")
            }
            else -> startGame()
        }
    }

    private fun importFrom(uri: Uri) {
        val bytes = readBytes(uri)
        when {
            bytes == null -> importError = "Could not read that file."
            NativeBridge.importSave(bytes) != 0 -> importError = "This .sav doesn't fit this game."
            else -> {
                askImport = false
                startGame()
            }
        }
    }

    private fun startGame() {
        keyboardMask = 0
        touchMask = 0
        fastSwitch.reset()
        fastOn = false
        repeatFilter.clear()
        controlsHidden = false
        if (hardware and (NativeBridge.HW_TILT or NativeBridge.HW_GYRO or NativeBridge.HW_SOLAR) != 0) {
            motion = MotionInput(this, hardware) { toast(it) }
        }
        if (hardware and NativeBridge.HW_RUMBLE != 0) rumble = Rumble(this)
        // A fresh SurfaceView for every session: Restart stops and starts within one frame, and the new render
        // thread only gets a window when the view's surface is created (the old one kept the last frame).
        session++
        sendCheats() // after the ROM, its save file and the automatic state: before the first frame
        NativeBridge.start()
        playing = true
        setFullScreen(true)
        startExtras()
    }

    /** The cartridge's sensors and rumble run only while the game does. */
    private fun startExtras() {
        motion?.start()
        rumble?.start()
    }

    private fun stopExtras() {
        motion?.stop()
        rumble?.stop()
    }

    private fun stopGame() {
        stopExtras()
        motion = null
        rumble = null
        NativeBridge.stop() // writes the save
        playing = false
        menu = null
        setFullScreen(false)
    }

    /** Back: the game pauses (sound, sensors, rumble too; the save file is written) behind the menu. */
    private fun openMenu() {
        stopExtras()
        NativeBridge.pause()
        slotsChanged++
        menu = MenuPage.MAIN
    }

    private fun resumeGame() {
        menu = null
        NativeBridge.resume()
        startExtras()
    }

    private fun saveTo(slot: Int) {
        replaceSlot = null
        if (states?.save(slot) == true) toast("Saved to slot $slot") else toast("Couldn't save the state.")
        slotsChanged++
        resumeGame()
    }

    private fun loadFrom(slot: Int) {
        if (states?.load(slot) == true) resumeGame()
        else toast("This state can't be loaded (damaged, or made by an older version).")
    }

    /** The moment the player left, for resuming it next time (the game must be paused). */
    private fun saveAuto() {
        if (playing) states?.save(States.AUTO)
    }

    private fun quitGame() {
        saveAuto()
        stopGame()
        refreshLibrary() // it is now first, with the picture of where it was left
    }

    private fun restartGame() {
        askRestart = false
        val g = current ?: return
        stopGame()
        open(g, resumeAuto = false)
    }

    /** Buttons to the game; ⏩ and ⟲ (touch or keys) to the switch and the emulation thread. */
    private fun sendKeys(now: Long = SystemClock.uptimeMillis()) {
        val all = KeyMap.merge(keyboardMask, touchMask)
        NativeBridge.setKeys(all and KeyMap.GBA_BUTTONS)
        val fastDown = all and KeyMap.FAST != 0
        if (fastDown != fastSwitch.held) {
            if (fastDown) fastSwitch.press(now) else fastSwitch.release(now)
        }
        fastOn = fastSwitch.switchedOn
        NativeBridge.setFastForward(if (fastSwitch.fast) fastSpeed else 0)
        NativeBridge.setRewinding(all and KeyMap.REWIND != 0)
    }

    private fun releaseRepeatedKeys() {
        for ((bit, upAt) in repeatFilter.due(SystemClock.uptimeMillis())) {
            keyboardMask = keyboardMask and bit.inv()
            sendKeys(upAt) // the hold ended at the key-up, which decides tap or hold
        }
    }

    /** The panel changed the list (the game is paused behind the menu): keep it and switch the cheats. */
    private fun changeCheats(list: List<Cheat>) {
        cheats = list
        if (cheatStore?.save(list) == false) toast("Couldn't save the cheats.")
        sendCheats()
    }

    /** The switched-on cheats into the emulator; one that no longer reads is switched off (and said so). */
    private fun sendCheats() {
        val bad = cheats.filter { it.on && NativeBridge.checkCheat(it.codes, it.type)[1] != 0 }
        if (bad.isNotEmpty()) {
            for (c in bad) toast("Cheat '${c.name}' can't be read and was switched off.")
            cheats = cheats.map { if (it in bad) it.copy(on = false) else it }
            cheatStore?.save(cheats)
        }
        val on = cheats.filter { it.on }
        if (NativeBridge.setCheats(on.map { it.codes }.toTypedArray(), on.map { it.type }.toIntArray()) != 0 && on.isNotEmpty()) {
            toast("The cheats couldn't be switched on.")
        }
    }

    /** A setting changed (Settings screen, menu row): kept, and used at once where it can be. */
    private fun changeSettings(s: Settings) {
        settings = s
        s.save(settingsStore)
        fastSpeed = s.fastSpeed
        applySettings(s)
        if (playing) sendKeys() // a switched-on fast mode uses the new speed
    }

    /** Picture and sound at once; rewind for the next game (the touch buttons read [settings] themselves). */
    private fun applySettings(s: Settings) {
        NativeBridge.setPicture(s.size, s.smooth)
        NativeBridge.setBackground(Skins.at(s.skin).body)
        NativeBridge.setSound(s.sound, s.soundBursts)
        NativeBridge.setRewindBudget(s.rewindBudget)
    }

    /** The layout editor turns the screen to the edited orientation (the phone may be held either way). */
    private fun openLayoutEditor(sideways: Boolean) {
        editPicture = if (playing) NativeBridge.frameBgr555()?.let(States::bitmapOf) else null // paused behind the menu
        orientationBeforeEditor = if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
            ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        } else ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        requestedOrientation = if (sideways) ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        editLayout = sideways
    }

    /**
     * Back the way the screen was, then free again. Found while prototyping: with auto-rotate off, Android keeps the
     * editor's orientation as the locked one, so just freeing the screen would leave it turned.
     */
    private fun closeLayoutEditor() {
        editLayout = null
        editPicture = null
        requestedOrientation = orientationBeforeEditor
        handler.postDelayed({
            if (editLayout == null) requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        }, 1000)
    }

    private fun biosPicked(uri: Uri) {
        val bytes = readBytes(uri) ?: run { toast("Could not read that file."); return }
        when (Bios.check(bytes)) {
            Bios.Check.WRONG_SIZE -> toast("That isn't a GBA BIOS (it must be 16 KB).")
            Bios.Check.UNUSUAL -> unusualBios = bytes
            Bios.Check.USUAL -> installBios(bytes)
        }
    }

    private fun installBios(bytes: ByteArray) {
        if (!Bios.install(filesDir, bytes)) {
            toast("Couldn't copy the BIOS file.")
            return
        }
        changeSettings(settings.copy(biosFile = true))
        toast("BIOS file chosen: it is used the next time a game opens.")
    }

    private fun toast(text: String) = Toast.makeText(this, text, Toast.LENGTH_LONG).show()

    private fun setFullScreen(on: Boolean) {
        val bars = WindowCompat.getInsetsController(window, window.decorView)
        if (on) {
            bars.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            bars.hide(WindowInsetsCompat.Type.systemBars())
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            bars.show(WindowInsetsCompat.Type.systemBars())
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        waitingKey?.let { (bit, device) -> // Settings → Keyboard & controller: this key is the button's now
            if (event.keyCode == KeyEvent.KEYCODE_BACK || event.keyCode == KeyEvent.KEYCODE_ESCAPE) {
                if (event.action == KeyEvent.ACTION_UP) waitingKey = null
            } else if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                keys.assign(bit, device, event.keyCode)
                keys.use()
                keysChanged++
                waitingKey = null
            }
            return true
        }
        if (event.keyCode == KeyEvent.KEYCODE_ESCAPE) { // Esc on a keyboard = Back (the menu)
            if (event.action == KeyEvent.ACTION_UP) onBackPressedDispatcher.onBackPressed()
            return true
        }
        val motionKey = MotionMath.motionKey(event.keyCode)
        val m = motion
        if (playing && motionKey != 0 && m != null) { // I/J/K/L tilt, Q/E turn
            when (event.action) {
                KeyEvent.ACTION_DOWN -> m.setKey(motionKey, true)
                KeyEvent.ACTION_UP -> m.setKey(motionKey, false)
            }
            controlsHidden = true
            return true
        }
        if (playing && KeyMap.isKeyboardExtra(event.keyCode)) { // F / R: see KeyRepeatFilter
            val bit = KeyMap.bitFor(event.keyCode)
            when (event.action) {
                KeyEvent.ACTION_DOWN -> if (repeatFilter.down(bit, event.eventTime)) {
                    keyboardMask = keyboardMask or bit
                    sendKeys(event.eventTime)
                }
                KeyEvent.ACTION_UP -> {
                    val wait = repeatFilter.up(bit, event.eventTime)
                    if (wait >= 0) handler.postDelayed(::releaseRepeatedKeys, wait)
                }
            }
            controlsHidden = true
            return true
        }
        if (playing && KeyMap.bitFor(event.keyCode) != 0) {
            when (event.action) {
                KeyEvent.ACTION_DOWN -> keyboardMask = KeyMap.update(keyboardMask, event.keyCode, true)
                KeyEvent.ACTION_UP -> keyboardMask = KeyMap.update(keyboardMask, event.keyCode, false)
            }
            controlsHidden = true // a keyboard or gamepad is in use
            sendKeys()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onPause() {
        super.onPause()
        stopExtras()
        NativeBridge.pause() // returns once the save is written
        saveAuto() // Home, another app, screen off: keep the moment
    }

    override fun onResume() {
        super.onResume()
        if (playing && menu == null && replaceSlot == null && !askRestart) {
            NativeBridge.resume()
            startExtras()
        }
        if (!playing) refreshLibrary() // games added to the folder meanwhile, back from a picker
    }

    override fun onDestroy() {
        if (isFinishing) NativeBridge.stop()
        super.onDestroy()
    }
}
```

- [x] **Step 4: The tests on the emulator, and today's**

```bash
bash tools/dev.sh apk 2>&1 | grep -E "^e: |^w: |BUILD"
rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |BUILD|Finished"
grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: |BUILD"
ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'
git diff proto-8b -- app platform | head -5
```

Expected: `BUILD SUCCESSFUL`, no `e:` / `w:` lines; on-device `tests="58" failures="0"`; Kotlin `96 0` (the wording test `UiTextTest` passes with the skin names); the diff against the prototype empty.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-8b-console-skins.md
git commit -m "feat(app): the touch buttons and the body in the skin; the Skin page" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6: Claude's check and milestone 8b exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test 2>&1 | tail -3
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: |BUILD"
ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'
rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | grep -E "BUILD|Finished"
grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
```

Expected: CTest `100% tests passed, 0 tests failed out of 120`; Kotlin `96 0`; on-device `tests="58" failures="0"`. The app (this branch) stays installed on the emulator for the check.

- [x] **Step 2: Claude's check on the emulator** (no user check in 8b)

Back up the user's app settings first and put them back at the end (`adb` = `"$LOCALAPPDATA/Android/Sdk/platform-tools/adb"`, with `MSYS_NO_PATHCONV=1`): `adb shell run-as com.tengen.gbaemu cat shared_prefs/settings.xml > <scratchpad>/settings-backup.xml`; note `adb shell settings get system user_rotation` and `accelerometer_rotation`. Use the test game `m7_demo` (not the user's game A). Helper `<scratchpad>/setskin.sh SKIN SIZE LOOK` (writes the app's settings and restarts it), with `ui.sh` (`tap X Y`, `key N`, `swipe`, `shot NAME`, `wait S`) from 8a's scratchpad:

```bash
#!/usr/bin/env bash
# setskin.sh SKIN SIZE LOOK — the app's settings for the 8b check (the user's own kept in settings-backup.xml)
A="$LOCALAPPDATA/Android/Sdk/platform-tools/adb"
D="$(dirname "$0")"
cat > "$D/settings-check.xml" <<X
<?xml version='1.0' encoding='utf-8' standalone='yes' ?>
<map>
    <int name="delay" value="0" />
    <int name="rewind_length" value="1" />
    <int name="fast_speed" value="-1" />
    <int name="size" value="$2" />
    <int name="rewind" value="1" />
    <int name="touch_look" value="$3" />
    <int name="sound" value="1" />
    <int name="bios_file" value="0" />
    <int name="buzz" value="0" />
    <int name="show_speed" value="0" />
    <int name="smooth" value="1" />
    <int name="touch_skin" value="$1" />
</map>
X
MSYS_NO_PATHCONV=1 "$A" shell am force-stop com.tengen.gbaemu
MSYS_NO_PATHCONV=1 "$A" push "$(cygpath -w "$D/settings-check.xml")" /data/local/tmp/settings-check.xml > /dev/null
MSYS_NO_PATHCONV=1 "$A" shell chmod 644 /data/local/tmp/settings-check.xml
MSYS_NO_PATHCONV=1 "$A" shell run-as com.tengen.gbaemu cp /data/local/tmp/settings-check.xml shared_prefs/settings.xml
MSYS_NO_PATHCONV=1 "$A" shell am start -n com.tengen.gbaemu/.MainActivity > /dev/null
sleep 3
```

Check, with screenshots (`adb exec-out screencap -p`) and pixel values read by a Python script (PIL):
1. Every skin 0-12 upright (Fit): a body pixel below the picture equals the skin's body colour (§2 of the spec); labels readable.
2. Every skin sideways (`settings put system accelerometer_rotation 0`, `user_rotation 1`): both side bars equal the body colour; the D-pad, A/B, L/R, ⟲ and ⏩ solid with the rim; only Select / Start see-through on the game (Plain: everything see-through white, as before).
3. Size = Whole steps upright (`setskin.sh N 1 0`) for Ice, Red and cream, Black: a pixel in the bar inside the game's surface and one below the picture both equal the body colour.
4. Sideways with Red and cream: Select / Start at Faint, Normal and Strong (`setskin.sh 10 0 1|0|2`) in the skin's red; a held A (`adb shell input swipe X Y X Y 4000` in the background, then a shot) drawn pressed; ⏩ tapped on: lit (amber, dark triangles).
5. Back → Settings: the Touch buttons summary "Red and cream · Normal · Buzz off"; the Touch buttons page's Skin row; the Skin page with the amber border on the chosen skin and rounded tiny phones; picking another skin; Back to Touch buttons showing it; Edit upright layout in the skin; Cancel.
6. Put the user's settings back (`run-as ... cp` from a pushed copy of the backup), the rotation back (`user_rotation 0`, `accelerometer_rotation 1`), remove `/data/local/tmp/settings-check.xml`.

Expected: everything as above (it matched in the prototype check, 2026-10-04).

- [x] **Step 3: Docs**

The 8b spec's status → implemented; the parent spec's status line and §10 row 8 → 8b ✅, milestone 8 ✅; `CLAUDE.md` key files → milestone 9 (button pictures + skin import) needs brainstorming; PROGRESS.md (Last updated, Goal, the status table — milestone 8 ✅ —, the 8b task table, Completed, Next: brainstorm milestone 9); every box of this plan ticked.

- [x] **Step 4: Commit, tag, merge**

```bash
git add docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-8b-console-skins.md docs/superpowers/specs/2026-10-04-milestone-8b-console-skins-design.md docs/superpowers/specs/2026-09-24-gba-emulator-design.md CLAUDE.md
git commit -m "docs: milestone 8b complete - console skins" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-8b
```

Then ask the user how to integrate (so far always a fast-forward merge into `main`); superpowers:finishing-a-development-branch; `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`; record the merge in PROGRESS.md. Delete the prototype worktree and branch (`rm -rf <scratchpad>/p8b` in Git Bash, `git worktree prune`, `git branch -D proto-8b`) with the user's OK; ask whether to keep or delete the branch `milestone-8` (milestone 8 is finished).
