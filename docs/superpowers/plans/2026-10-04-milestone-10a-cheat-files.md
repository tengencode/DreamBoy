# Milestone 10a — Cheat Files — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** the user picks a libretro `.cht` cheat file in a game's cheat panel; all its cheats are added switched off (unreadable ones and duplicates skipped, with a note); a Remove all button empties the list.

**Architecture:** Pure Kotlin `ChtFile` reads the file into (name, codes) pairs, merges them into the game's list and words the note — all tested on the PC. Each cheat is checked with the core's existing `NativeBridge.checkCheat`. `CheatPanel` gets the two buttons and the Remove all question; `MainActivity` opens Android's file picker and saves through the existing `changeCheats`. The emulator core and the native code are not touched.

**Tech Stack:** Kotlin, Jetpack Compose (Material 3 1.4.0), `ActivityResultContracts.OpenDocument`, JUnit 4, Compose UI test 1.11.4, Gradle `--offline`, Git Bash `tools/dev.sh`.

**Spec:** `docs/superpowers/specs/2026-10-04-milestone-10a-cheat-files-design.md`

---

## Conventions

- Git Bash from the repo root, on the branch `milestone-10`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push>` (the Pixel 9 phone emulator `Pixel_9_API_36_1`; never the user's USB phones unless the user asks). Gradle runs `--offline`; nothing new is downloaded by the build. A missing artifact → stop and ask the user. Ask before restarting or cold-booting the emulator.
- The core and the native code are not touched: CTest stays 120/120 (run once in Task 5).
- Counting tests: Kotlin unit tests `ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'` (total, failures); on-device `grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml` after `rm -rf app/build/outputs/androidTest-results`.
- Kotlin: 119 → 125 (Task 1) → 128 (Task 2). On-device: 63 → 64 (Task 3).
- Test-first: each task's tests are written first and fail (they don't compile without the new code), then the code.
- One commit per task; messages end with the Co-Authored-By trailer. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes). The tasks run one after another without stopping; the only stop is Task 5's download question (real libretro `.cht` files — ask the user before each download). No user check (spec §1). After every task: all documents updated and checked for anything stale.
- Every string the app shows must pass `UiTextTest` (no "…", "...", "&").

## File map

```
app/src/main/java/com/tengen/gbaemu/ChtFile.kt (new); app/src/test/java/com/tengen/gbaemu/ChtFileTest.kt (new)        Tasks 1, 2
app/src/main/java/com/tengen/gbaemu/Screens.kt; app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt              Task 3
app/src/main/java/com/tengen/gbaemu/MainActivity.kt                                                                    Task 4
(Claude's check on the emulator, docs, tag)                                                                            Task 5
```

---

### Task 1: Reading a `.cht` file

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/ChtFile.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/ChtFileTest.kt` (new, 6 tests)

- [x] **Step 1: Write the failing test**

Create `app/src/test/java/com/tengen/gbaemu/ChtFileTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class ChtFileTest {
    /** Two cheats as libretro-database's GBA files write them (read 2026-10-04). */
    private val real = """
        cheats = 19

        cheat0_desc = "Infinite HP"
        cheat0_code = "42000538+07CF+00000004+014C"
        cheat0_enable = false

        cheat1_desc = "Master Code"
        cheat1_code = "D8BAE4D9+4864DCE5+A86CDBA5+19BA49B3"
        cheat1_enable = false
    """.trimIndent()

    @Test
    fun realFilesGiveNamesAndCodesAsLines() {
        assertEquals(listOf(
            "Infinite HP" to "42000538 07CF\n00000004 014C",
            "Master Code" to "D8BAE4D9 4864DCE5\nA86CDBA5 19BA49B3",
        ), ChtFile.parse(real))
    }

    @Test
    fun quotesBareValuesAndSpacesRoundTheEqualsSign() {
        val text = listOf(
            "# a comment = not a cheat",
            "cheat0_desc=\"Say \\\"hi\\\" C:\\\\x\"",
            "cheat0_code=\"0200A000:63\"",
            "just some words",
            "cheat1_desc   =   Bare name",
            "cheat1_code = 42000538+07CF",
        ).joinToString("\n")
        assertEquals(listOf("Say \"hi\" C:\\x" to "0200A000:63", "Bare name" to "42000538 07CF"), ChtFile.parse(text))
    }

    @Test
    fun windowsLineEndsAndAByteOrderMark() {
        assertEquals(ChtFile.parse(real), ChtFile.parse("\uFEFF" + real.replace("\n", "\r\n")))
    }

    @Test
    fun cheatsComeInNumberOrderAndNeedACode() {
        val text = """
            cheat2_desc = "Third"
            cheat2_code = "42000538+07CF"
            cheat0_desc = "First"
            cheat0_code = "D8BAE4D9+4864DCE5"
            cheat1_desc = "No code"
            cheat3_desc = "Empty code"
            cheat3_code = ""
            cheat4_code = "0200A000:63"
            cheat5_desc = ""
            cheat5_code = "0200A001:01"
            cheat6_desc = "Memory search"
            cheat6_address = "33816576"
            cheat6_handler = "1"
            cheat6_value = "99"
        """.trimIndent()
        assertEquals(listOf(
            "First" to "D8BAE4D9 4864DCE5",
            "Third" to "42000538 07CF",
            "Cheat 5" to "0200A000:63",
            "Cheat 6" to "0200A001:01",
        ), ChtFile.parse(text))
    }

    @Test
    fun aFileThatIsNotACheatFileGivesNothing() {
        val romBytes = String(ByteArray(4096) { (it * 31 + 7).toByte() }, Charsets.UTF_8)
        assertEquals(emptyList<Pair<String, String>>(), ChtFile.parse(romBytes))
        assertEquals(emptyList<Pair<String, String>>(), ChtFile.parse(""))
        assertEquals(emptyList<Pair<String, String>>(), ChtFile.parse("cheats = 0\n"))
    }

    @Test
    fun codePartsPairAnAddressWithItsValue() {
        assertEquals("42000538 07CF", ChtFile.codeLines("42000538+07CF"))
        assertEquals("D8BAE4D9 4864DCE5", ChtFile.codeLines("D8BAE4D9+4864DCE5"))
        assertEquals("42000538 07CF\n00000004", ChtFile.codeLines("42000538+07CF+00000004")) // an odd count: the last alone
        assertEquals("0200A000:63\n42000538 07CF", ChtFile.codeLines("0200A000:63+42000538+07CF"))
        assertEquals("820258400063", ChtFile.codeLines("8202 5840\t0063")) // spaces inside a part removed
        assertEquals("42000538 07CF", ChtFile.codeLines("+42000538++07CF+")) // empty parts dropped
        assertEquals("ABCDEFGH\n07CF", ChtFile.codeLines("ABCDEFGH+07CF")) // not hex: no pair
        assertEquals("", ChtFile.codeLines(" + "))
    }
}
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -5`
Expected: FAIL — compilation error, `Unresolved reference 'ChtFile'`.

- [x] **Step 3: Write `ChtFile.kt` (reading only)**

Create `app/src/main/java/com/tengen/gbaemu/ChtFile.kt`:

```kotlin
package com.tengen.gbaemu

/**
 * libretro's `.cht` cheat files (milestone 10a): `cheatN_desc = "name"` and `cheatN_code = "A+B+C"` lines, read into
 * names and codes as the cheat pop-up takes them (one code per line). Pure: tested on the PC.
 */
object ChtFile {
    private val KEY = Regex("""cheat(\d+)_(desc|code)""")
    private val HEX = Regex("[0-9A-Fa-f]+")

    /** (name, codes) per cheat that has a code, in N order; empty for anything that isn't a `.cht`. */
    fun parse(text: String): List<Pair<String, String>> {
        val names = HashMap<Int, String>()
        val codes = HashMap<Int, String>()
        for (raw in text.removePrefix("\uFEFF").lines()) {
            val line = raw.trim()
            if (line.isEmpty() || line.startsWith("#")) continue
            val eq = line.indexOf('=')
            if (eq < 0) continue
            val key = KEY.matchEntire(line.substring(0, eq).trim()) ?: continue
            val n = key.groupValues[1].toIntOrNull() ?: continue
            val value = unquote(line.substring(eq + 1).trim())
            if (key.groupValues[2] == "desc") names[n] = value else codes[n] = value
        }
        return codes.keys.sorted().mapNotNull { n ->
            val lines = codeLines(codes.getValue(n))
            if (lines.isEmpty()) null else (names[n]?.takeIf { it.isNotBlank() } ?: "Cheat ${n + 1}") to lines
        }
    }

    /**
     * A file's code (parts joined with `+`) as lines: an 8-hex-digit part and a 4- or 8-hex-digit part after it make
     * one line `AAAAAAAA BBBB`; any other part is a line on its own. Spaces inside a part are removed.
     */
    fun codeLines(code: String): String {
        val parts = code.split('+').map { p -> p.filterNot { it == ' ' || it == '\t' } }.filter { it.isNotEmpty() }
        val lines = mutableListOf<String>()
        var i = 0
        while (i < parts.size) {
            val a = parts[i]
            val b = parts.getOrNull(i + 1)
            if (a.length == 8 && HEX.matches(a) && b != null && (b.length == 4 || b.length == 8) && HEX.matches(b)) {
                lines += "$a $b"
                i += 2
            } else {
                lines += a
                i++
            }
        }
        return lines.joinToString("\n")
    }

    /** A value in quotes (where `\"` and `\\` stand for `"` and `\`), or a bare one as it is. */
    private fun unquote(v: String): String {
        if (!v.startsWith('"')) return v
        val out = StringBuilder()
        var i = 1
        while (i < v.length) {
            val c = v[i]
            if (c == '"') break
            if (c == '\\' && i + 1 < v.length && (v[i + 1] == '"' || v[i + 1] == '\\')) {
                out.append(v[i + 1])
                i += 2
                continue
            }
            out.append(c)
            i++
        }
        return out.toString()
    }
}
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -5`, then the Kotlin count command (Conventions).
Expected: `BUILD SUCCESSFUL`; `125 0`.

- [x] **Step 5: Docs and commit**

`docs/PROGRESS.md`: Completed entry "M10a Task 1", the 10a task table, Next → Task 2; this plan's Task 1 boxes ticked. Check for stale lines.

```bash
git add app/src/main/java/com/tengen/gbaemu/ChtFile.kt app/src/test/java/com/tengen/gbaemu/ChtFileTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10a-cheat-files.md
git commit -m "feat: read libretro .cht cheat files (ChtFile.parse, codeLines)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Merging into a game's list, and the note

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/ChtFile.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/ChtFileTest.kt` (+3 tests)

- [x] **Step 1: Write the failing tests**

Add to `ChtFileTest` (inside the class):

```kotlin
    private val mine = Cheat("Walk through walls", NativeBridge.CHEAT_CODEBREAKER, "82025840 0063", true)

    @Test
    fun newCheatsGoAfterTheGamesOwnSwitchedOffAndAuto() {
        val m = ChtFile.merge(listOf(mine), listOf("A" to "42000538 07CF", "B" to "D8BAE4D9 4864DCE5")) { true }
        assertEquals(listOf(mine,
            Cheat("A", NativeBridge.CHEAT_AUTO, "42000538 07CF", false),
            Cheat("B", NativeBridge.CHEAT_AUTO, "D8BAE4D9 4864DCE5", false)), m.cheats)
        assertEquals(ChtFile.Merge(m.cheats, added = 2, unreadable = 0, already = 0), m)
    }

    @Test
    fun duplicatesAndUnreadableCheatsAreSkippedAndCounted() {
        val found = listOf(
            "Walk through walls" to "82025840 0063",  // in the list: spaces and case don't matter
            "Walk through walls" to "8202584O 0063",   // another code (a letter O): new, but unreadable
            "Money" to "42000538 07cf",
            "Money" to "4200053807CF",                 // the same inside the file
            "Bad" to "XYZ",
        )
        val readable = { codes: String -> "O" !in codes && codes != "XYZ" }
        val m = ChtFile.merge(listOf(mine.copy(codes = "8202 5840
0063")), found, readable)
        assertEquals(listOf("Walk through walls", "Money"), m.cheats.map { it.name })
        assertEquals("42000538 07cf", m.cheats[1].codes) // kept as the file wrote it
        assertEquals(1, m.added)
        assertEquals(2, m.unreadable)
        assertEquals(2, m.already)
    }

    @Test
    fun theNoteSaysWhatHappened() {
        fun note(added: Int, unreadable: Int, already: Int) = ChtFile.note(ChtFile.Merge(emptyList(), added, unreadable, already))
        assertEquals("Added 72 cheats.", note(72, 0, 0))
        assertEquals("Added 1 cheat.", note(1, 0, 3))
        assertEquals("Added 70 cheats. 2 couldn't be read.", note(70, 2, 0))
        assertEquals("Added 5 cheats. 1 couldn't be read.", note(5, 1, 0))
        assertEquals("No new cheats in this file.", note(0, 0, 4))
        assertEquals("No new cheats in this file. 1 couldn't be read.", note(0, 1, 4))
        assertEquals("No cheats could be read from this file.", note(0, 3, 0))
        assertEquals("This file has no cheats.", ChtFile.NO_CHEATS)
    }
```

- [x] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -5`
Expected: FAIL — `Unresolved reference 'merge'` / `'Merge'` / `'note'` / `'NO_CHEATS'` (`MAX_BYTES` is used in Task 4).

- [x] **Step 3: Add `Merge`, `merge`, `note` and `NO_CHEATS`**

In `ChtFile.kt`, inside `object ChtFile`, before `private val KEY`:

```kotlin
    /** What an import did: the game's new list, and how many were added, couldn't be read, were already there. */
    data class Merge(val cheats: List<Cheat>, val added: Int, val unreadable: Int, val already: Int)

    /** The note when the file has no cheat at all (none with a code, not text, too big, can't be opened). */
    const val NO_CHEATS = "This file has no cheats."

    /** The biggest file read: libretro's GBA files are far smaller. */
    const val MAX_BYTES = 1 shl 20
```

and after `codeLines`:

```kotlin
    /**
     * The game's cheats, then the file's new ones (switched off, type Auto). One with the same name and codes as a
     * cheat already in the list (or earlier in the file) is already there; one [readable] refuses is skipped.
     */
    fun merge(existing: List<Cheat>, found: List<Pair<String, String>>, readable: (String) -> Boolean): Merge {
        val seen = existing.mapTo(HashSet()) { same(it.name, it.codes) }
        val out = existing.toMutableList()
        var unreadable = 0
        var already = 0
        for ((name, codes) in found) {
            when {
                !seen.add(same(name, codes)) -> already++
                !readable(codes) -> unreadable++
                else -> out += Cheat(name, NativeBridge.CHEAT_AUTO, codes, on = false)
            }
        }
        return Merge(out, out.size - existing.size, unreadable, already)
    }

    /** The note shown after an import that found cheats. */
    fun note(m: Merge): String {
        val first = when {
            m.added == 1 -> "Added 1 cheat."
            m.added > 1 -> "Added ${m.added} cheats."
            m.already > 0 -> "No new cheats in this file."
            else -> "No cheats could be read from this file."
        }
        return if (m.unreadable > 0 && (m.added > 0 || m.already > 0)) "$first ${m.unreadable} couldn't be read." else first
    }

    /** Codes compare without spaces or line breaks, in any case. */
    private fun same(name: String, codes: String) = name to codes.filterNot(Char::isWhitespace).uppercase()
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -5`, then the Kotlin count command.
Expected: `BUILD SUCCESSFUL`; `128 0` (`UiTextTest` passes the new words).

- [x] **Step 5: Docs and commit**

`docs/PROGRESS.md` (Completed "M10a Task 2", task table, Next → Task 3); this plan's boxes ticked. Check for stale lines.

```bash
git add app/src/main/java/com/tengen/gbaemu/ChtFile.kt app/src/test/java/com/tengen/gbaemu/ChtFileTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10a-cheat-files.md
git commit -m "feat: merge a cheat file's cheats into a game's list, and the note

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The cheat panel's Import cheat file and Remove all

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt` (`CheatPanel`, around line 529)
- Test: `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt` (+1 test)

- [x] **Step 1: Write the failing test**

Add to `ScreensTest` (inside the class):

```kotlin
    @Test
    fun theCheatPanelImportsAFileAndRemovesAllAfterAQuestion() {
        var imported = false
        var removed = 0
        val list = mutableStateOf(listOf(Cheat("A", 0, "0200A000:63", false), Cheat("B", 0, "0200A001:01", true)))
        compose.setContent {
            AppTheme {
                CheatPanel("game C", list.value, onToggle = { _, _ -> }, onEdit = {}, onAdd = {}, onBack = {},
                    onImport = { imported = true }, onRemoveAll = { removed++ })
            }
        }
        compose.onNodeWithText("Import cheat file").performClick()
        assertEquals(true, imported)
        compose.onNodeWithText("Remove all").performClick()
        compose.onNodeWithText("Remove all 2 cheats?").assertIsDisplayed()
        compose.onNodeWithText("Cancel").performClick()
        assertEquals(0, removed)
        compose.onNodeWithText("Remove all").performClick()
        compose.onNodeWithText("Remove").performClick()
        assertEquals(1, removed)
        list.value = list.value.take(1)
        compose.onNodeWithText("Remove all").performClick()
        compose.onNodeWithText("Remove the 1 cheat?").assertIsDisplayed()
        compose.onNodeWithText("Cancel").performClick()
        list.value = emptyList()
        compose.onNodeWithText("Remove all").assertDoesNotExist()
        compose.onNodeWithText("Import cheat file").assertIsDisplayed()
    }
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh androidtest 2>&1 | tail -5`
Expected: FAIL — compilation error, `No parameter with name 'onImport'`.

- [x] **Step 3: The buttons and the question**

In `Screens.kt`, replace `CheatPanel`'s signature and its Add cheat button block with:

```kotlin
fun CheatPanel(title: String, cheats: List<Cheat>, onToggle: (Int, Boolean) -> Unit, onEdit: (Int) -> Unit,
               onAdd: () -> Unit, onBack: () -> Unit, onImport: () -> Unit = {}, onRemoveAll: () -> Unit = {}) {
    var askRemoveAll by remember { mutableStateOf(false) }
```

(the body up to `Spacer(Modifier.height(10.dp))` unchanged), then:

```kotlin
                Spacer(Modifier.height(10.dp))
                Button(onClick = onAdd, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Icon(Icons.Filled.Add, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Add cheat")
                }
                Spacer(Modifier.height(8.dp))
                OutlinedButton(onClick = onImport, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Icon(Icons.Filled.FileUpload, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Import cheat file")
                }
                if (cheats.isNotEmpty()) {
                    Spacer(Modifier.height(8.dp))
                    OutlinedButton(onClick = { askRemoveAll = true }, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error)) {
                        Text("Remove all")
                    }
                }
            }
        }
    }
    if (askRemoveAll) {
        AlertDialog(
            onDismissRequest = { askRemoveAll = false },
            title = { Text(if (cheats.size == 1) "Remove the 1 cheat?" else "Remove all ${cheats.size} cheats?") },
            text = { Text("The cheats and their codes are removed from this game.") },
            confirmButton = { DangerButton("Remove") { askRemoveAll = false; onRemoveAll() } },
            dismissButton = { TextButton(onClick = { askRemoveAll = false }) { Text("Cancel") } },
        )
    }
}
```

(`OutlinedButton`, `ButtonDefaults`, `Icons.Filled.FileUpload`, `AlertDialog`, `remember`, `mutableStateOf` are already imported in `Screens.kt`.)

(Done with one addition: the inner `Column` and the list's `Column` also got `Modifier.weight(1f, fill = false)`, so on a short sideways screen the list shrinks and the buttons stay visible.)

- [x] **Step 4: Run the tests**

Run: `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | tail -5`, then the on-device count command; also `bash tools/dev.sh jvmtest 2>&1 | tail -3` (`UiTextTest`).
Expected: on-device `64`, 0 failures; Kotlin `128 0`.

- [x] **Step 5: Docs and commit**

`docs/PROGRESS.md` (Completed "M10a Task 3", task table, Next → Task 4); this plan's boxes ticked. Check for stale lines.

```bash
git add app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10a-cheat-files.md
git commit -m "feat: Import cheat file and Remove all in the cheat panel

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Picking the file and importing it

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/MainActivity.kt` (the pickers near line 112; the `MenuPage.CHEATS` branch near line 219; new functions after `changeCheats`, near line 720)

No new test: the pieces are tested in Tasks 1–3; the wiring is checked by Claude on the emulator (Task 5).

- [x] **Step 1: The picker**

After `pickSkin` (near line 112):

```kotlin
    private val pickCht = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importCheats(uri)
    }
```

- [x] **Step 2: The panel's two callbacks**

Replace the `MenuPage.CHEATS` branch with:

```kotlin
                    MenuPage.CHEATS -> CheatPanel(currentTitle, cheats,
                        onToggle = { i, on -> changeCheats(cheats.toMutableList().also { it[i] = it[i].copy(on = on) }) },
                        onEdit = { editCheat = it }, onAdd = { editCheat = -1 }, onBack = { menu = MenuPage.MAIN },
                        onImport = { pickCht.launch(arrayOf("*/*")) }, onRemoveAll = { changeCheats(emptyList()) })
```

(`*/*`: `.cht` has no MIME type. The menu stays open while the picker is shown, so `onResume` keeps the game paused.)

- [x] **Step 3: Reading and merging**

After `sendCheats()`:

```kotlin
    /** Adds a picked `.cht` file's cheats to the game's list, switched off; a note says what happened (10a). */
    private fun importCheats(uri: Uri) {
        val text = readUpTo(uri, ChtFile.MAX_BYTES)?.takeIf { it.size <= ChtFile.MAX_BYTES }?.toString(Charsets.UTF_8)
        val found = text?.let(ChtFile::parse).orEmpty()
        if (found.isEmpty()) {
            toast(ChtFile.NO_CHEATS)
            return
        }
        val m = ChtFile.merge(cheats, found) { NativeBridge.checkCheat(it, NativeBridge.CHEAT_AUTO)[1] == 0 }
        if (m.added > 0) changeCheats(m.cheats)
        toast(ChtFile.note(m))
    }

    /** At most [max] + 1 bytes of the file (so a bigger one is seen without reading it all), or null. */
    private fun readUpTo(uri: Uri, max: Int): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use { input ->
            val out = ByteArrayOutputStream()
            val buf = ByteArray(8192)
            while (out.size() <= max) {
                val n = input.read(buf)
                if (n < 0) break
                out.write(buf, 0, n)
            }
            out.toByteArray()
        }
    } catch (e: Exception) {
        null
    }
```

and add `import java.io.ByteArrayOutputStream` beside `import java.io.File`.

- [x] **Step 4: Build and run all tests**

Run: `bash tools/dev.sh apk 2>&1 | tail -3 && bash tools/dev.sh jvmtest 2>&1 | tail -3`
Expected: `BUILD SUCCESSFUL` both; Kotlin `128 0`.

- [x] **Step 5: Docs and commit**

`docs/PROGRESS.md` (Completed "M10a Task 4", task table, Next → Task 5); this plan's boxes ticked. Check for stale lines.

```bash
git add app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10a-cheat-files.md
git commit -m "feat: import a picked .cht file into the game's cheats

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Claude's check, milestone 10a exit

**Files:** docs only (`docs/PROGRESS.md`, this plan, the spec's status, the parent spec's status line and §10 row 10, `CLAUDE.md` key files).

- [x] **Step 1: Full runs**

```bash
bash tools/dev.sh test 2>&1 | tail -3
bash tools/dev.sh jvmtest 2>&1 | tail -3
rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | tail -3
```

Expected: CTest `100% tests passed, 0 tests failed out of 120`; Kotlin `128 0`; on-device 64, 0 failures.

- [x] **Step 2: Real `.cht` files (ask the user first — a stop)**

Ask the user (layman mode) to OK downloading, from GitHub's libretro/libretro-database (`cht/Nintendo - Game Boy Advance/`), `game A - game A Version (USA, Europe) (Code Breaker).cht` (a few KB) into `tests/cht/` (add `tests/cht/` to `.gitignore`; never committed). With the OK:

```bash
mkdir -p tests/cht && gh api -H "Accept: application/vnd.github.raw" \
  "repos/libretro/libretro-database/contents/cht/Nintendo%20-%20Game%20Boy%20Advance/game A%20-%20Green%20Version%20(USA%2C%20Europe)%20(Code%20Breaker).cht" \
  > "tests/cht/game A - game A Version (USA, Europe) (Code Breaker).cht"
grep -c '_code = ' "tests/cht/game A - game A Version (USA, Europe) (Code Breaker).cht"
bash tools/dev.sh push "tests/cht/game A - game A Version (USA, Europe) (Code Breaker).cht"
```

Note the file's cheat count (the `grep -c`), and work out the expected "Added N" with a quick JVM run if any code might not read (or compare with the panel's count).

- [x] **Step 3: Claude's check on the emulator** (`bash tools/dev.sh install`; back up the user's cheats first: `adb shell run-as com.tengen.gbaemu ls files/cheats`, each file copied out with `adb exec-out run-as com.tengen.gbaemu cat files/cheats/<id>.json` into the scratchpad; put them back at the end). With screenshots (`adb exec-out screencap -p`) and taps (`adb shell input tap`), in the user's `hack-1.gba`:
  1. Pause menu → Cheats → **Import cheat file** → the picker → the game A `.cht`: the note "Added N cheats." (N as expected; "M couldn't be read." if any), the names in the panel, all switched off, Remove all shown.
  2. Tap a cheat: its pop-up shows its codes as lines (`AAAAAAAA BBBB` or `AAAAAAAA BBBBBBBB`); Cancel.
  3. Switch one on, Resume: no "can't be read" note, the game plays on; back to Cheats, switch it off.
  4. Import the same file again → "No new cheats in this file." (the list unchanged).
  5. Import `m7_demo.gba` (already in Download; not text) → "This file has no cheats."; `hack-1.gba` (bigger than 1 MB) → the same.
  6. Remove all → "Remove all N cheats?" → Cancel (still there) → Remove all → Remove: "No cheats yet." and no Remove all button; quit and reopen the game: still empty.
  7. Put the user's cheats files back (`adb push` to `/data/local/tmp`, then `run-as com.tengen.gbaemu cp`).
  Anything wrong → superpowers:systematic-debugging, fix test-first, commit, re-check.

- [x] **Step 4: Docs, tag and the merge question**

The spec's status → implemented (milestone 10a complete, Claude's check passed); the parent spec's status line and §10 row 10 → 10a ✅, 10b next; `CLAUDE.md` key files → current milestone 10b box art (needs brainstorming), last finished: this spec and plan; `docs/PROGRESS.md` → Completed (each task's hash and notes, the check), Project status (10a ✅), Next (brainstorm 10b box art); every box in this plan ticked. Check every document for anything stale. Commit (`docs: milestone 10a complete - cheat files`), `git tag milestone-10a`, then superpowers:finishing-a-development-branch (the user picks merge / PR / keep).
