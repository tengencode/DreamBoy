# Milestone 10b — Box Art (+ cheat files online) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** the game grid's small cards show each game's box, downloaded from libretro in the background; Choose / Remove box art in the hold menu; a Settings switch; Find cheats online in the cheat panel.

**Architecture:** Pure Kotlin reads libretro's game list (`GameNames`), remembers what's known per game (`BoxArt`), matches cheat files (`CheatSources`), downloads (`Net`, plain `HttpURLConnection`) and runs the lookups (`BoxArtHelper`, with downloading, reading a game and shrinking a picture passed in) — all tested on the PC, `Net` against a tiny local server. Android code shrinks pictures (`BoxPicture`), draws the square cards, the hold-menu items, the switch and the online-cheats pop-up, and `MainActivity` runs the helper on a background thread while the game list shows. The emulator core and the native code are not touched.

**Tech Stack:** Kotlin, Jetpack Compose (Material 3 1.4.0, material-icons-extended 1.7.8), `HttpURLConnection`, `BitmapFactory`, `URLEncoder`, JUnit 4, Compose UI test 1.11.4, Gradle `--offline`, Git Bash `tools/dev.sh`.

**Spec:** `docs/superpowers/specs/2026-10-04-milestone-10b-box-art-design.md`

---

## Conventions

- Git Bash from the repo root, on the branch `milestone-10b`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push>` (the Pixel 9 phone emulator `Pixel_9_API_36_1`; never the user's USB phones unless the user asks). Gradle runs `--offline`; nothing new is downloaded by the build. A missing artifact → stop and ask the user. Ask before restarting or cold-booting the emulator.
- The core and the native code are not touched: CTest stays 120/120 (run once in Task 9).
- Counting tests: Kotlin unit tests `ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'` (total, failures); on-device `grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml` after `rm -rf app/build/outputs/androidTest-results`.
- Kotlin: 128 → 135 (Task 1) → 140 (Task 2) → 143 (Task 3) → 146 (Task 4) → 152 (Task 5). On-device: 64 → 66 (Task 6) → 70 (Task 7).
- Test-first: each task's tests are written first and fail (they don't compile without the new code), then the code.
- One commit per task; messages end with the Co-Authored-By trailer. Each commit also includes `docs/PROGRESS.md` (the task's Completed entry, the 10b task table, Project status, Next, Last updated) and this plan (ticked boxes). The tasks run one after another without stopping; no user check (spec §1). The user's design approval covers Task 9's downloads (the game list, about 3 boxes, 1–2 cheat files) — nothing else is downloaded.
- Every string in `src/main` must pass `UiTextTest` (no "…", "...", "&" in a string literal — the picture-name rule's `&` is a `Char`, not a string; no raw string ending in four quotes, which its reader misreads).
- Unit tests never touch the internet: `Net` is tested against a local server, the helper with a fake `fetch`.

## File map

```
app/src/main/java/com/tengen/gbaemu/GameNames.kt (new); app/src/test/.../GameNamesTest.kt (new)                 Task 1
app/src/main/java/com/tengen/gbaemu/BoxArt.kt (new); app/src/test/.../BoxArtTest.kt (new)                       Task 2
app/src/main/java/com/tengen/gbaemu/CheatSources.kt (new); app/src/test/.../CheatSourcesTest.kt (new)           Task 3
app/src/main/java/com/tengen/gbaemu/Net.kt (new), MainActivity.kt (readUpTo), AndroidManifest.xml;
  app/src/test/.../NetTest.kt (new)                                                                             Task 4
app/src/main/java/com/tengen/gbaemu/BoxArtHelper.kt (new); app/src/test/.../BoxArtHelperTest.kt (new)           Task 5
app/src/main/java/com/tengen/gbaemu/BoxPicture.kt (new); app/src/androidTest/.../BoxPictureTest.kt (new)         Task 6
app/src/main/java/com/tengen/gbaemu/Settings.kt, SettingsScreens.kt, Screens.kt;
  app/src/test/.../SettingsTest.kt; app/src/androidTest/.../ScreensTest.kt                                      Task 7
app/src/main/java/com/tengen/gbaemu/MainActivity.kt                                                             Task 8
(Claude's check on the emulator, docs, tag)                                                                     Task 9
```

(`app/src/test/...` = `app/src/test/java/com/tengen/gbaemu/`; `app/src/androidTest/...` = `app/src/androidTest/java/com/tengen/gbaemu/`.)

---

### Task 1: libretro's game list (`GameNames`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/GameNames.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/GameNamesTest.kt` (new, 7 tests)

- [x] **Step 1: Write the failing test**

Create `app/src/test/java/com/tengen/gbaemu/GameNamesTest.kt` (the excerpt is copied from libretro-database's `metadat/no-intro/Nintendo - Game Boy Advance.dat`, version 2026.08.01; md5/sha1 cut for length — the reader ignores them):

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class GameNamesTest {
    private val dat = """
        clrmamepro (
        	name "Nintendo - Game Boy Advance"
        	description "Nintendo - Game Boy Advance"
        	version "2026.08.01"
        )

        game (
        	name "2 in 1 Game Pack - Spider-Man + Spider-Man 2 (USA) (En,Fr,De+En,Fr,De,Es)"
        	rom ( name "2 in 1 Game Pack - Spider-Man + Spider-Man 2 (USA) (En,Fr,De+En,Fr,De,Es).gba" size 33554432 crc 3ABC30BD md5 DDCA8CBB )
        )

        game (
        	name "game A - game A Version (USA, Europe)"
        	region "USA"
        	serial "BPEE"
        	rom ( name "game A - game A Version (USA, Europe).gba" size 16777216 crc 1F1C08FB md5 605B89B6 serial "BPEE" )
        )

        game (
        	name "game A - game A Version (USA, Europe) (Rev 1)"
        	region "USA"
        	serial "AXVE"
        	rom ( name "game A - game A Version (USA, Europe) (Rev 1).gba" size 16777216 crc 61641576 md5 E0503182 serial "AXVE" )
        )

        game (
        	name "game A - game A Version (USA, Europe)"
        	region "USA"
        	serial "AXVE"
        	rom ( name "game A - game A Version (USA, Europe).gba" size 16777216 crc F0815EE7 md5 53D1A202 serial "AXVE" )
        )

        game (
        	name "game A - game A Version (USA, Europe) (Rev 2)"
        	region "USA"
        	serial "AXVE"
        	rom ( name "game A - game A Version (USA, Europe) (Rev 2).gba" size 16777216 crc AEAC73E6 md5 3E1B2065 serial "AXVE" )
        )
    """.trimIndent()

    private val names = GameNames.fromDat(dat)

    @Test
    fun aRealExcerptIsReadAndGamesAreFoundByFingerprint() {
        assertEquals(5, names.rows.size)
        assertEquals("game A - game A Version (USA, Europe)", names.find("1f1c08fb", ""))
        assertEquals("game A - game A Version (USA, Europe)", names.find("1F1C08FB", "")) // any case
        assertEquals("game A - game A Version (USA, Europe) (Rev 2)", names.find("aeac73e6", "AXVE")) // the fingerprint first
        assertEquals(GameNames.Row("1f1c08fb", "BPEE", "game A - game A Version (USA, Europe)"), names.rows[1])
    }

    @Test
    fun aHackIsFoundByItsCode() {
        assertEquals("game A - game A Version (USA, Europe)", names.find("12345678", "BPEE"))
        assertNull(names.find("12345678", "ZZZZ"))
        assertNull(names.find("12345678", ""))
    }

    @Test
    fun theplainestNameWinsWhenGamesShareACode() {
        assertEquals("game A - game A Version (USA, Europe)", names.find("12345678", "AXVE"))
    }

    @Test
    fun aRomWithoutACodeIsFoundOnlyByFingerprint() {
        assertEquals("2 in 1 Game Pack - Spider-Man + Spider-Man 2 (USA) (En,Fr,De+En,Fr,De,Es)", names.find("3abc30bd", ""))
        assertEquals("", names.rows[0].code)
        assertEquals(emptyList<GameNames.Row>(), GameNames.fromDat("not a list at all\n)\ngame (\n").rows)
    }

    @Test
    fun theKeptCopyReadsBackTheSame() {
        val back = GameNames.fromTsv("#time\t123\n" + names.toTsv() + "broken line\n\n")
        assertEquals(names.rows, back.rows)
        assertEquals("game A - game A Version (USA, Europe)", back.find("", "AXVE"))
    }

    @Test
    fun theCodeComesFromTheHeader() {
        fun rom(code: String) = ByteArray(0xC0).also { code.toByteArray(Charsets.ISO_8859_1).copyInto(it, 0xAC) }
        assertEquals("BPEE", GameNames.code(rom("BPEE")))
        assertEquals("A2B9", GameNames.code(rom("A2B9")))
        assertEquals("", GameNames.code(rom("bpee"))) // capitals and digits only
        assertEquals("", GameNames.code(rom("AB")))   // two zero bytes after
        assertEquals("", GameNames.code(ByteArray(0xAE)))
    }

    @Test
    fun pictureNamesReplaceLibretrosSymbols() {
        assertEquals("game A - game A Version (USA, Europe)", GameNames.pictureName("game A - game A Version (USA, Europe)"))
        assertEquals("Lilo _ Stitch_ A_B_c_d_e_f_g_h_i", GameNames.pictureName("Lilo & Stitch: A/B*c`d<e>f?g\\h|i"))
    }
}
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest 2>&1 | grep -E "^e:|BUILD" | head -3`
Expected: FAIL — `Unresolved reference 'GameNames'`.

- [x] **Step 3: Write `GameNames.kt`**

```kotlin
package com.tengen.gbaemu

/**
 * libretro's list of GBA games (10b): each ROM's fingerprint (CRC32, as [GameId] writes it), its 4-letter game code
 * and the game's name — the name box art and cheat files are filed under. Read from the No-Intro `.dat`, kept as
 * tab-separated lines. Pure: tested on the PC.
 */
class GameNames(val rows: List<Row>) {
    /** One ROM: its fingerprint (8 lowercase hex digits), its code ("" if none) and its game's name. */
    data class Row(val id: String, val code: String, val name: String)

    private val byId = rows.associate { it.id to it.name }
    private val byCode = rows.filter { validCode(it.code) }.groupBy { it.code }
        .mapValues { (_, same) -> same.map { it.name }.minWith(compareBy<String> { it.length }.thenBy { it }) }

    /** The game's name: by its fingerprint, else by its header code (the plainest name of those sharing it). */
    fun find(id: String, code: String): String? = byId[id.lowercase()] ?: if (validCode(code)) byCode[code] else null

    fun toTsv(): String = rows.joinToString("") { "${it.id}\t${it.code}\t${it.name}\n" }

    companion object {
        private val CODE = Regex("[A-Z0-9]{4}")
        private val CRC = Regex("""\bcrc ([0-9A-Fa-f]{8})\b""")
        private val SERIAL = Regex("\\bserial \"([^\"]*)\"")
        private val NAME = Regex("name \"([^\"]*)\"")

        /** The characters libretro replaces with `_` in picture file names. */
        private val REPLACED = setOf('&', '*', '/', ':', '`', '<', '>', '?', '\\', '|')

        fun validCode(code: String) = CODE.matches(code)

        /** A clrmamepro `.dat`: per `game ( … )` its name, and per `rom ( … )` line its crc and serial. */
        fun fromDat(text: String): GameNames {
            val rows = mutableListOf<Row>()
            var inGame = false
            var name: String? = null
            var serial = ""
            for (raw in text.lines()) {
                val line = raw.trim()
                if (line == "game (") {
                    inGame = true
                    name = null
                    serial = ""
                    continue
                }
                if (!inGame) continue
                when {
                    line == ")" -> inGame = false
                    line.startsWith("name \"") && name == null -> name = NAME.find(line)?.groupValues?.get(1)
                    line.startsWith("serial \"") -> serial = SERIAL.find(line)?.groupValues?.get(1) ?: ""
                    line.startsWith("rom (") -> {
                        val rest = line.replaceFirst(NAME, "") // the ROM's file name could hold anything
                        val crc = CRC.find(rest)?.groupValues?.get(1)?.lowercase()
                        val game = name
                        if (crc != null && game != null) rows += Row(crc, SERIAL.find(rest)?.groupValues?.get(1) ?: serial, game)
                    }
                }
            }
            return GameNames(rows)
        }

        /** What [toTsv] writes; lines starting with `#` and broken lines are skipped. */
        fun fromTsv(text: String): GameNames = GameNames(text.lines().mapNotNull { line ->
            val parts = line.split('\t')
            if (line.startsWith("#") || parts.size != 3 || parts[0].length != 8) null else Row(parts[0], parts[1], parts[2])
        })

        /** The ROM header's game code (4 bytes at 0xAC) if it is 4 capitals or digits, else "". */
        fun code(rom: ByteArray): String {
            if (rom.size < 0xB0) return ""
            return String(rom, 0xAC, 4, Charsets.ISO_8859_1).takeIf(::validCode) ?: ""
        }

        /** The game's name as libretro names its pictures (without `.png`). */
        fun pictureName(name: String): String = name.map { if (it in REPLACED) '_' else it }.joinToString("")
    }
}
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3`, then the Kotlin count command.
Expected: `BUILD SUCCESSFUL`; `135 0`.

- [x] **Step 5: Docs and commit**

`docs/PROGRESS.md`: Completed "M10b Task 1", the 10b task table, Project status, Next → Task 2, Last updated; this plan's Task 1 boxes ticked. Check for stale lines.

```bash
git add app/src/main/java/com/tengen/gbaemu/GameNames.kt app/src/test/java/com/tengen/gbaemu/GameNamesTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10b-box-art.md
git commit -m "feat: libretro's GBA game list - names by fingerprint or game code

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: What's remembered per game (`BoxArt`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/BoxArt.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/BoxArtTest.kt` (new, 5 tests)

- [x] **Step 1: Write the failing test**

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BoxArtTest {
    private val dir = File(Files.createTempDirectory("boxart").toFile(), "boxart")
    private val jpeg = byteArrayOf(1, 2, 3)

    @Test
    fun aNewGameNeedsALookupAndAFoundOneKeepsItsBox() {
        val art = BoxArt(dir)
        assertTrue(art.needsLookup("1f1c08fb", 0))
        art.found("1f1c08fb", "game A - game A Version (USA, Europe)", jpeg, 500)
        assertFalse(art.needsLookup("1f1c08fb", 1000))
        assertArrayEquals(jpeg, art.picture("1f1c08fb")!!.readBytes())
        val again = BoxArt(dir) // read back from boxart.json
        assertEquals(BoxArt.Entry(BoxArt.State.FOUND, "game A - game A Version (USA, Europe)", 500), again.entry("1f1c08fb"))
        assertTrue(again.picture("1f1c08fb")!!.isFile)
    }

    @Test
    fun aGameNotFoundIsLookedUpAgainOnlyAfterANewerList() {
        val art = BoxArt(dir)
        art.none("12345678", "", 100)
        assertNull(art.picture("12345678"))
        assertFalse(art.needsLookup("12345678", 50))
        assertFalse(art.needsLookup("12345678", 100))
        assertTrue(art.needsLookup("12345678", 150))
    }

    @Test
    fun theUsersOwnPictureAndARemovedBoxAreNeverLookedUp() {
        val art = BoxArt(dir)
        art.none("12345678", "game A - game A Version (USA, Europe)", 100)
        assertTrue(art.own("12345678", jpeg))
        assertEquals(BoxArt.State.OWN, art.entry("12345678")!!.state)
        assertEquals("game A - game A Version (USA, Europe)", art.entry("12345678")!!.name) // kept for the cheats
        assertFalse(art.needsLookup("12345678", Long.MAX_VALUE))
        art.remove("12345678")
        assertNull(art.picture("12345678"))
        assertFalse(File(dir, "12345678.jpg").exists())
        assertEquals(BoxArt.State.REMOVED, art.entry("12345678")!!.state)
        assertFalse(art.needsLookup("12345678", Long.MAX_VALUE))
        assertTrue(art.own("12345678", jpeg)) // chosen again
        assertTrue(art.picture("12345678")!!.isFile)
    }

    @Test
    fun aFoundGameWhosePictureIsGoneIsLookedUpAgain() {
        val art = BoxArt(dir)
        art.found("1f1c08fb", "game A - game A Version (USA, Europe)", jpeg, 500)
        File(dir, "1f1c08fb.jpg").delete()
        assertNull(art.picture("1f1c08fb"))
        assertTrue(art.needsLookup("1f1c08fb", 0))
    }

    @Test
    fun aDamagedMemoryFileIsKeptAsideAndEverythingStartsAgain() {
        dir.mkdirs()
        File(dir, "boxart.json").writeText("{ broken")
        val art = BoxArt(dir)
        assertNull(art.entry("1f1c08fb"))
        assertTrue(File(dir, "boxart.json.bad").isFile)
        art.none("1f1c08fb", "", 1)
        assertEquals(BoxArt.State.NONE, BoxArt(dir).entry("1f1c08fb")!!.state)
    }
}
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest 2>&1 | grep -E "^e:|BUILD" | head -3`
Expected: FAIL — `Unresolved reference 'BoxArt'`.

- [x] **Step 3: Write `BoxArt.kt`**

```kotlin
package com.tengen.gbaemu

import java.io.File

/**
 * The game list's box art (10b), in `files/boxart/`: what is known per game (by its id) in `boxart.json` — found
 * (its box saved), none (looked up, not found), own (a picture the user chose), removed — and the pictures as
 * `<id>.jpg`. Used from the box-art thread and the UI thread, so every method is synchronized.
 */
class BoxArt(val dir: File) {
    enum class State { FOUND, NONE, OWN, REMOVED }

    /** A game's state, the name it was found under ("" if none) and when it was looked up. */
    data class Entry(val state: State, val name: String = "", val checked: Long = 0)

    private val file = File(dir, "boxart.json")
    private val entries = load()

    @Synchronized
    fun entry(id: String): Entry? = entries[id]

    /** The game's box, if it has one (found or chosen) and its file is there. */
    @Synchronized
    fun picture(id: String): File? {
        val s = entries[id]?.state
        return pictureFile(id).takeIf { (s == State.FOUND || s == State.OWN) && it.isFile }
    }

    /** No entry; none, looked up before the game list of [listTime]; found, but its picture is gone. Never own / removed. */
    @Synchronized
    fun needsLookup(id: String, listTime: Long): Boolean {
        val e = entries[id] ?: return true
        return when (e.state) {
            State.NONE -> e.checked < listTime
            State.FOUND -> !pictureFile(id).isFile
            State.OWN, State.REMOVED -> false
        }
    }

    @Synchronized
    fun found(id: String, name: String, jpeg: ByteArray, now: Long) {
        if (writePicture(id, jpeg)) put(id, Entry(State.FOUND, name, now))
    }

    @Synchronized
    fun none(id: String, name: String, now: Long) = put(id, Entry(State.NONE, name, now))

    /** The user's own picture (false if it couldn't be written); the name the game was found under is kept. */
    @Synchronized
    fun own(id: String, jpeg: ByteArray): Boolean {
        if (!writePicture(id, jpeg)) return false
        put(id, Entry(State.OWN, entries[id]?.name ?: "", entries[id]?.checked ?: 0))
        return true
    }

    @Synchronized
    fun remove(id: String) {
        pictureFile(id).delete()
        put(id, Entry(State.REMOVED, entries[id]?.name ?: "", entries[id]?.checked ?: 0))
    }

    private fun pictureFile(id: String) = File(dir, "$id.jpg")

    private fun writePicture(id: String, jpeg: ByteArray): Boolean = try {
        dir.mkdirs()
        val tmp = File(dir, "$id.jpg.tmp")
        tmp.writeBytes(jpeg)
        val f = pictureFile(id)
        tmp.renameTo(f) || (f.delete() && tmp.renameTo(f))
    } catch (x: Exception) {
        false
    }

    private fun put(id: String, e: Entry) {
        entries[id] = e
        try {
            dir.mkdirs()
            val tmp = File(file.path + ".tmp")
            tmp.writeText(toJson(entries))
            if (!tmp.renameTo(file)) {
                file.delete()
                tmp.renameTo(file)
            }
        } catch (x: Exception) {
            // kept in memory; written with the next change
        }
    }

    /** The entries; empty without a file, or if it is damaged (it is then kept as `.json.bad`). */
    private fun load(): MutableMap<String, Entry> {
        if (!file.isFile) return mutableMapOf()
        return try {
            fromJson(file.readText())
        } catch (x: Exception) {
            file.renameTo(File(file.path + ".bad"))
            mutableMapOf()
        }
    }

    companion object {
        fun toJson(entries: Map<String, Entry>): String = entries.entries.joinToString(",\n", "{\n", "\n}\n") { (id, e) ->
            "  ${Json.str(id)}: {\"state\": ${Json.str(e.state.name.lowercase())}, \"name\": ${Json.str(e.name)}, " +
                "\"checked\": ${e.checked}}"
        }

        fun fromJson(json: String): MutableMap<String, Entry> = Json.Reader(json).run {
            val out = mutableMapOf<String, Entry>()
            obj { id ->
                var state: State? = null
                var name = ""
                var checked = 0L
                obj { key ->
                    when (key) {
                        "state" -> state = State.valueOf(string().uppercase())
                        "name" -> name = string()
                        "checked" -> checked = number()
                        else -> error("unknown key $key")
                    }
                }
                out[id] = Entry(state ?: error("no state for $id"), name, checked)
            }
            end()
            out
        }
    }
}
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3`, then the Kotlin count command.
Expected: `BUILD SUCCESSFUL`; `140 0`.

- [x] **Step 5: Docs and commit**

`docs/PROGRESS.md` (Completed "M10b Task 2", the task table, Next → Task 3, Last updated); this plan's boxes ticked; check for stale lines.

```bash
git add app/src/main/java/com/tengen/gbaemu/BoxArt.kt app/src/test/java/com/tengen/gbaemu/BoxArtTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10b-box-art.md
git commit -m "feat: remember each game's box art (found, none, own, removed)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: libretro's cheat files for a game (`CheatSources`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/CheatSources.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/CheatSourcesTest.kt` (new, 3 tests)

- [x] **Step 1: Write the failing test**

(The file names are real ones from libretro-database's `cht/Nintendo - Game Boy Advance/`, listed 2026-10-04; the listing's shape is GitHub's contents reply, shortened.)

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CheatSourcesTest {
    private val files = listOf(
        "game A - game A Version (USA, Europe) (Code Breaker).cht",
        "game A - game A Version (USA, Europe) (Rev 1).cht",
        "game A - game A Version (USA, Europe) (Rev 1) (Code Breaker).cht",
        "Dungeons _ Dragons - Eye of the Beholder (USA, Europe) (Code Breaker).cht",
        "game N (USA, Australia).cht",
    )

    @Test
    fun githubsListingGivesTheCheatFileNames() {
        val json = """[
            {"name": "007 - Everything or Nothing (World) (Code Breaker).cht", "path": "cht/x", "size": 627, "type": "file",
             "_links": {"self": "https://api.github.com/x"}},
            {"name": "readme.txt", "type": "file", "size": 3},
            {"name": "old", "type": "dir", "size": 0}
        ]"""
        assertEquals(listOf("007 - Everything or Nothing (World) (Code Breaker).cht"), CheatSources.fromListing(json))
        assertNull(CheatSources.fromListing("{\"message\": \"API rate limit exceeded\"}"))
        assertNull(CheatSources.fromListing("<html>"))
    }

    @Test
    fun aGamesFilesAreItsNameOrItsNameAndOneLabel() {
        assertEquals(listOf("Code Breaker" to files[0]), CheatSources.forGame(files, "game A - game A Version (USA, Europe)"))
        assertEquals(listOf("Cheats" to files[1]), CheatSources.forGame(files, "game A - game A Version (USA, Europe) (Rev 1)"))
        assertEquals(emptyList<Pair<String, String>>(), CheatSources.forGame(files, "game A - game A Version (USA, Europe)"))
        assertEquals(listOf("Code Breaker" to files[2]), CheatSources.forGame(files, "game A - game A Version (USA, Europe) (Rev 1)"))
    }

    @Test
    fun namesWithSymbolsAreMatchedAsIsOrAsPictureNames() {
        assertEquals(listOf("Code Breaker" to files[3]),
            CheatSources.forGame(files, "Dungeons & Dragons - Eye of the Beholder (USA, Europe)"))
        assertEquals(listOf("Cheats" to files[4]), CheatSources.forGame(files, "game N (USA, Australia)"))
        val two = listOf("Game (USA) (GameShark).cht", "Game (USA) (Code Breaker).cht")
        assertEquals(listOf("Code Breaker", "GameShark"), CheatSources.forGame(two, "Game (USA)").map { it.first }) // A-Z
    }
}
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest 2>&1 | grep -E "^e:|BUILD" | head -3`
Expected: FAIL — `Unresolved reference 'CheatSources'`.

- [x] **Step 3: Write `CheatSources.kt`**

```kotlin
package com.tengen.gbaemu

/** libretro's `.cht` cheat files (10b): which of them belong to a game, by the game's name. Pure: tested on the PC. */
object CheatSources {
    const val NONE_FOUND = "No cheat files found for this game."
    const val NO_INTERNET = "Couldn't reach the internet."

    /** The `.cht` file names in GitHub's reply listing a folder; null if the reply isn't such a list. */
    fun fromListing(json: String): List<String>? = try {
        val all = Json.Reader(json).run { value().also { end() } } as? List<*>
        all?.mapNotNull { item ->
            val m = item as? Map<*, *> ?: return@mapNotNull null
            (m["name"] as? String)?.takeIf { m["type"] == "file" && it.endsWith(".cht") }
        }
    } catch (e: Exception) {
        null
    }

    /**
     * The files made for the game called [name], as (label, file name), A-Z by label: `<name>.cht` ("Cheats") or
     * `<name> (<label>).cht` with exactly one more bracket part; the name as is or as libretro writes picture names.
     */
    fun forGame(files: List<String>, name: String): List<Pair<String, String>> {
        val bases = listOf(name, GameNames.pictureName(name)).distinct()
        return files.mapNotNull { f ->
            val stem = f.removeSuffix(".cht")
            bases.firstNotNullOfOrNull { b ->
                when {
                    stem == b -> "Cheats"
                    stem.startsWith("$b (") && stem.endsWith(")") ->
                        stem.substring(b.length + 2, stem.length - 1).takeIf { '(' !in it && ')' !in it }
                    else -> null
                }
            }?.let { it to f }
        }.sortedBy { it.first }
    }
}
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3`, then the Kotlin count command.
Expected: `BUILD SUCCESSFUL`; `143 0` (`UiTextTest` passes the two notes).

- [x] **Step 5: Docs and commit**

`docs/PROGRESS.md` (Completed "M10b Task 3", the task table, Next → Task 4, Last updated); this plan's boxes ticked; check for stale lines.

```bash
git add app/src/main/java/com/tengen/gbaemu/CheatSources.kt app/src/test/java/com/tengen/gbaemu/CheatSourcesTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10b-box-art.md
git commit -m "feat: match libretro's cheat files to a game

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: One download (`Net`), the internet permission

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/Net.kt`
- Modify: `app/src/main/AndroidManifest.xml` (after the VIBRATE permission); `app/src/main/java/com/tengen/gbaemu/MainActivity.kt` (10a's `readUpTo` and `importCheats` use `Net.readUpTo`)
- Test: `app/src/test/java/com/tengen/gbaemu/NetTest.kt` (new, 3 tests; a tiny HTTP server on 127.0.0.1 inside the test — plain HTTP works on the PC's Java)

- [x] **Step 1: Write the failing test**

```kotlin
package com.tengen.gbaemu

import java.net.InetAddress
import java.net.ServerSocket
import kotlin.concurrent.thread
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NetTest {
    /** A tiny HTTP server on this machine: each path's status and body; the last request's header lines. */
    private class Server(val replies: Map<String, Pair<Int, ByteArray>>) : AutoCloseable {
        val socket = ServerSocket(0, 50, InetAddress.getLoopbackAddress())
        @Volatile var lastRequest = ""
        val url = "http://127.0.0.1:${socket.localPort}"

        init {
            thread(isDaemon = true) {
                while (!socket.isClosed) {
                    val s = try { socket.accept() } catch (e: Exception) { break }
                    s.use {
                        val reader = s.getInputStream().bufferedReader()
                        val head = generateSequence { reader.readLine() }.takeWhile { it.isNotEmpty() }.toList()
                        lastRequest = head.joinToString("\n")
                        val path = head.firstOrNull()?.split(" ")?.getOrNull(1) ?: "/"
                        val (status, body) = replies[path] ?: (404 to ByteArray(0))
                        val out = s.getOutputStream()
                        out.write("HTTP/1.1 $status X\r\nContent-Length: ${body.size}\r\nConnection: close\r\n\r\n".toByteArray())
                        out.write(body)
                        out.flush()
                    }
                }
            }
        }

        override fun close() = socket.close()
    }

    @Test
    fun aReplyIsReadWholeAndA404IsMissing() {
        Server(mapOf("/ok" to (200 to "hello".toByteArray()))).use { s ->
            val got = Net.get("${s.url}/ok", 100)
            assertTrue(got is Net.Got)
            assertEquals("hello", String((got as Net.Got).bytes))
            assertTrue(s.lastRequest, "User-Agent: ${Net.USER_AGENT}" in s.lastRequest)
            assertEquals(Net.Missing, Net.get("${s.url}/gone", 100))
        }
    }

    @Test
    fun tooBigOrAServerErrorIsAFailure() {
        Server(mapOf("/big" to (200 to ByteArray(100)), "/err" to (500 to ByteArray(0)))).use { s ->
            assertEquals(Net.Failed, Net.get("${s.url}/big", 99))
            assertTrue(Net.get("${s.url}/big", 100) is Net.Got) // exactly the limit is fine
            assertEquals(Net.Failed, Net.get("${s.url}/err", 100))
        }
    }

    @Test
    fun nothingListeningOrNoAddressIsAFailure() {
        val port = ServerSocket(0).use { it.localPort } // free again once closed
        assertEquals(Net.Failed, Net.get("http://127.0.0.1:$port/x", 100))
        assertEquals(Net.Failed, Net.get("not a url", 100))
    }
}
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest 2>&1 | grep -E "^e:|BUILD" | head -3`
Expected: FAIL — `Unresolved reference 'Net'`.

- [x] **Step 3: Write `Net.kt`**

```kotlin
package com.tengen.gbaemu

import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL

/** One download (10b): its bytes, missing (HTTP 404) or failed (anything else). */
object Net {
    sealed interface Result
    class Got(val bytes: ByteArray) : Result
    data object Missing : Result
    data object Failed : Result

    const val USER_AGENT = "GBAEmulator/1.0 (Android)"
    private const val TIMEOUT = 15_000

    /** GET [url] (15 s to connect and to read, redirects followed); more than [max] bytes is a failure. */
    fun get(url: String, max: Int): Result = try {
        val c = URL(url).openConnection() as HttpURLConnection
        try {
            c.connectTimeout = TIMEOUT
            c.readTimeout = TIMEOUT
            c.instanceFollowRedirects = true
            c.setRequestProperty("User-Agent", USER_AGENT)
            when (c.responseCode) {
                200 -> c.inputStream.use { readUpTo(it, max) }?.let(::Got) ?: Failed
                404 -> Missing
                else -> Failed
            }
        } finally {
            c.disconnect()
        }
    } catch (e: Exception) {
        Failed
    }

    /** All of [input], or null if it is more than [max] bytes (reading stops there). */
    fun readUpTo(input: InputStream, max: Int): ByteArray? {
        val out = ByteArrayOutputStream()
        val buf = ByteArray(8192)
        while (true) {
            val n = input.read(buf)
            if (n < 0) return out.toByteArray()
            out.write(buf, 0, n)
            if (out.size() > max) return null
        }
    }
}
```

- [x] **Step 4: The permission, and 10a's reading through `Net.readUpTo`**

`AndroidManifest.xml`, after the VIBRATE line:

```xml
    <!-- Box art and cheat files from libretro (10b); granted without asking. -->
    <uses-permission android:name="android.permission.INTERNET" />
```

`MainActivity.kt`: replace 10a's `readUpTo` with

```kotlin
    /** The file's bytes, or null if it can't be read or is more than [max] bytes (not read whole). */
    private fun readUpTo(uri: Uri, max: Int): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use { Net.readUpTo(it, max) }
    } catch (e: Exception) {
        null
    }
```

in `importCheats` replace the first line with `val text = readUpTo(uri, ChtFile.MAX_BYTES)?.toString(Charsets.UTF_8)`, and remove the now unused `import java.io.ByteArrayOutputStream`.

- [x] **Step 5: Run the tests and build**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3`, the Kotlin count command, then `bash tools/dev.sh apk 2>&1 | tail -2`.
Expected: `BUILD SUCCESSFUL`; `146 0`; the app builds.

- [x] **Step 6: Docs and commit**

`docs/PROGRESS.md` (Completed "M10b Task 4", the task table, Next → Task 5, Last updated); this plan's boxes ticked; check for stale lines.

```bash
git add app/src/main/java/com/tengen/gbaemu/Net.kt app/src/test/java/com/tengen/gbaemu/NetTest.kt app/src/main/AndroidManifest.xml app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10b-box-art.md
git commit -m "feat: one download (Net) and the internet permission

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The lookups (`BoxArtHelper`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/BoxArtHelper.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/BoxArtHelperTest.kt` (new, 6 tests)

- [x] **Step 1: Write the failing test**

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BoxArtHelperTest {
    private val game_a = "game A - game A Version (USA, Europe)"
    private val dat = """
        clrmamepro (
        	name "Nintendo - Game Boy Advance"
        )

        game (
        	name "game A - game A Version (USA, Europe)"
        	serial "BPEE"
        	rom ( name "game A - game A Version (USA, Europe).gba" size 16777216 crc 1F1C08FB serial "BPEE" )
        )
    """.trimIndent()
    private val sampleBox = BoxArtHelper.BOX_URL + "game A%20-%20Green%20Version%20%28USA%2C%20Europe%29.png"
    private val hack = ByteArray(0x200).also { "BPEE".toByteArray().copyInto(it, 0xAC) } // a hack: game A's code
    private val homebrew = ByteArray(0x200) // no code

    /** Replies by URL (unlisted: missing), and every URL asked for. */
    private class FakeNet(val replies: MutableMap<String, Net.Result>) {
        val asked = mutableListOf<String>()
        fun fetch(url: String, max: Int): Net.Result {
            asked += url
            return replies[url] ?: Net.Missing
        }
    }

    private var clock = 1_000_000_000L
    private val read = mutableListOf<String>() // the keys of the games read
    private fun tmp() = File(Files.createTempDirectory("boxart").toFile(), "boxart")
    private fun helper(dir: File, net: FakeNet, art: BoxArt = BoxArt(dir), roms: Map<String, ByteArray> = emptyMap()) =
        BoxArtHelper(dir, art, net::fetch, { g -> read += g.key; roms[g.key] }, { it }, { clock })
    private fun game(key: String, id: String = "") = GameEntry(key, "content://$key", key, id)
    private fun withList() = FakeNet(mutableMapOf(BoxArtHelper.NAMES_URL to Net.Got(dat.toByteArray()),
        sampleBox to Net.Got(byteArrayOf(9, 9))))

    @Test
    fun theGameListIsDownloadedOnceAndKeptForAMonth() {
        val dir = tmp()
        val net = withList()
        assertEquals(game_a, helper(dir, net).names()?.find("1f1c08fb", ""))
        fun downloads() = net.asked.count { it == BoxArtHelper.NAMES_URL }
        assertEquals(1, downloads())
        clock += BoxArtHelper.MONTH - 1
        helper(dir, net).names()
        assertEquals(1, downloads())
        clock += 2
        net.replies[BoxArtHelper.NAMES_URL] = Net.Failed
        val h = helper(dir, net)
        assertNotNull(h.names()) // the old one, as the new one failed
        assertEquals(2, downloads())
        assertEquals(1_000_000_000L, h.listTime)
        assertNull(helper(tmp(), FakeNet(mutableMapOf(BoxArtHelper.NAMES_URL to Net.Failed))).names()) // nothing kept
        assertEquals("game A%20-%20Green%20Version%20%28USA%2C%20Europe%29.png", BoxArtHelper.encode("$game_a.png"))
    }

    @Test
    fun aKnownGameGetsItsBoxWithoutReadingTheGame() {
        val dir = tmp()
        val art = BoxArt(dir)
        var pictures = 0
        helper(dir, withList(), art).run(listOf(game("e", "1f1c08fb")), onId = { _, _ -> }, onPicture = { pictures++ })
        assertEquals(BoxArt.Entry(BoxArt.State.FOUND, game_a, clock), art.entry("1f1c08fb"))
        assertEquals(listOf<Byte>(9, 9), art.picture("1f1c08fb")!!.readBytes().toList())
        assertEquals(1, pictures)
        assertEquals(emptyList<String>(), read)
    }

    @Test
    fun aHackIsFoundByItsCodeAndItsIdIsPassedOn() {
        val dir = tmp()
        val art = BoxArt(dir)
        val ids = mutableListOf<String>()
        helper(dir, withList(), art, mapOf("h" to hack)).run(listOf(game("h")), onId = { g, id -> ids += "${g.key}=$id" }, onPicture = {})
        val id = GameId.of(hack)
        assertEquals(listOf("h=$id"), ids)
        assertEquals(BoxArt.State.FOUND, art.entry(id)!!.state)
        assertEquals(game_a, art.entry(id)!!.name)
    }

    @Test
    fun unknownGamesAndMissingBoxesAreRememberedAndAFailureStopsTheRun() {
        val dir = tmp()
        val art = BoxArt(dir)
        val net = withList()
        net.replies[sampleBox] = Net.Missing
        val games = listOf(game("b", GameId.of(homebrew)), game("e", "1f1c08fb"))
        helper(dir, net, art, mapOf("b" to homebrew)).run(games, { _, _ -> }, {})
        assertEquals(BoxArt.Entry(BoxArt.State.NONE, "", clock), art.entry(GameId.of(homebrew)))
        assertEquals(BoxArt.Entry(BoxArt.State.NONE, game_a, clock), art.entry("1f1c08fb")) // known, but no box yet
        val asked = net.asked.size
        helper(dir, net, art, mapOf("b" to homebrew)).run(games, { _, _ -> }, {})
        assertEquals(asked, net.asked.size) // the same list: nothing looked up again

        val dir2 = tmp()
        val art2 = BoxArt(dir2)
        val net2 = withList()
        net2.replies[sampleBox] = Net.Failed
        read.clear()
        helper(dir2, net2, art2, mapOf("h" to hack)).run(listOf(game("e", "1f1c08fb"), game("h")), { _, _ -> }, {})
        assertNull(art2.entry("1f1c08fb")) // nothing written: tried again next time
        assertEquals(emptyList<String>(), read) // the run stopped before the hack
    }

    @Test
    fun stopEndsTheRunBeforeTheNextGame() {
        val dir = tmp()
        val art = BoxArt(dir)
        val h = helper(dir, withList(), art, mapOf("h" to hack))
        h.run(listOf(game("e", "1f1c08fb"), game("h")), onId = { _, _ -> }, onPicture = { h.stop() })
        assertTrue(art.picture("1f1c08fb") != null)
        assertFalse("h" in read)
    }

    @Test
    fun cheatFilesAreListedOnceAndMatchedToTheGame() {
        val dir = tmp()
        val file = "$game_a (Code Breaker).cht"
        val listing = """[{"name": "$file", "type": "file"}, {"name": "Other (USA).cht", "type": "file"}]"""
        val net = FakeNet(mutableMapOf(BoxArtHelper.CHEAT_LIST_URL to Net.Got(listing.toByteArray()),
            BoxArtHelper.CHEAT_URL + BoxArtHelper.encode(file) to Net.Got("cheat0_desc = \"x\"".toByteArray())))
        assertEquals(listOf("Code Breaker" to file), helper(dir, net).cheatFiles(game_a))
        assertEquals(listOf("Code Breaker" to file), helper(dir, net).cheatFiles(game_a))
        assertEquals(1, net.asked.count { it == BoxArtHelper.CHEAT_LIST_URL }) // kept
        assertEquals("cheat0_desc = \"x\"", helper(dir, net).cheatFile(file))
        assertNull(helper(dir, net).cheatFile("Missing (USA).cht"))
        assertNull(helper(tmp(), FakeNet(mutableMapOf(BoxArtHelper.CHEAT_LIST_URL to Net.Failed))).cheatFiles(game_a))
    }
}
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest 2>&1 | grep -E "^e:|BUILD" | head -3`
Expected: FAIL — `Unresolved reference 'BoxArtHelper'`.

- [x] **Step 3: Write `BoxArtHelper.kt`**

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.net.URLEncoder

/**
 * Box art and cheat files from libretro (10b). [run] works through the game list one game at a time until [stop]:
 * libretro's game list first (kept 30 days as `names.tsv`), then each game that needs a lookup ([BoxArt]): its
 * fingerprint and header code → its name → its box, shrunk and saved. Pure — downloading, reading a game and shrinking
 * a picture are passed in — so it is tested on the PC; MainActivity runs it on a background thread.
 */
class BoxArtHelper(
    private val dir: File,
    private val boxArt: BoxArt,
    private val fetch: (url: String, max: Int) -> Net.Result,
    private val readGame: (GameEntry) -> ByteArray?,
    private val shrink: (ByteArray) -> ByteArray?,
    private val now: () -> Long = System::currentTimeMillis,
) {
    @Volatile
    private var stopped = false

    /** When the game list in use was downloaded (0 = none yet). */
    var listTime = 0L
        private set

    /** Ends [run] before its next game. */
    fun stop() {
        stopped = true
    }

    /**
     * Looks up [games] in turn. [onId] gets a game's fingerprint when it had to be read; [onPicture] follows each new
     * box. A box missing on the server is remembered as none; any other failure ends the run (tried again next time);
     * a picture that can't be read is skipped (tried again next time).
     */
    fun run(games: List<GameEntry>, onId: (GameEntry, String) -> Unit, onPicture: () -> Unit) {
        val names = names() ?: return
        for (g in games) {
            if (stopped) return
            if (g.id.isNotEmpty() && !boxArt.needsLookup(g.id, listTime)) continue
            var id = g.id
            var name = if (id.isEmpty()) null else names.find(id, "")
            if (name == null) {
                val rom = readGame(g) ?: continue
                if (id.isEmpty()) {
                    id = GameId.of(rom)
                    onId(g, id)
                    if (!boxArt.needsLookup(id, listTime)) continue
                }
                name = names.find(id, GameNames.code(rom))
            }
            if (name == null) {
                boxArt.none(id, "", now())
                continue
            }
            when (val r = fetch(BOX_URL + encode(GameNames.pictureName(name) + ".png"), MAX_PICTURE)) {
                is Net.Got -> {
                    val jpeg = shrink(r.bytes) ?: continue
                    boxArt.found(id, name, jpeg, now())
                    onPicture()
                }
                Net.Missing -> boxArt.none(id, name, now())
                Net.Failed -> return
            }
        }
    }

    /** The game list: the kept one if younger than 30 days, else downloaded (the old one if that fails); null if none. */
    fun names(): GameNames? {
        val file = File(dir, "names.tsv")
        val kept = readKept(file)
        if (kept != null && now() - kept.first < MONTH) {
            listTime = kept.first
            return GameNames.fromTsv(kept.second)
        }
        val got = fetch(NAMES_URL, MAX_LIST)
        if (got is Net.Got) {
            val names = GameNames.fromDat(got.bytes.toString(Charsets.UTF_8))
            if (names.rows.isNotEmpty()) {
                val t = now()
                writeKept(file, t, names.toTsv())
                listTime = t
                return names
            }
        }
        if (kept == null) return null
        listTime = kept.first
        return GameNames.fromTsv(kept.second)
    }

    /** libretro's cheat files for the game called [name], as (label, file); null if the list can't be had. */
    fun cheatFiles(name: String): List<Pair<String, String>>? {
        val file = File(dir, "cheat-files.txt")
        val kept = readKept(file)
        val files = if (kept != null && now() - kept.first < MONTH) lines(kept.second) else {
            val got = fetch(CHEAT_LIST_URL, MAX_LISTING)
            val listed = (got as? Net.Got)?.let { CheatSources.fromListing(it.bytes.toString(Charsets.UTF_8)) }
            if (listed != null) {
                writeKept(file, now(), listed.joinToString("") { "$it\n" })
                listed
            } else kept?.let { lines(it.second) } ?: return null
        }
        return CheatSources.forGame(files, name)
    }

    /** One cheat file's text, or null if it can't be had. */
    fun cheatFile(file: String): String? =
        (fetch(CHEAT_URL + encode(file), ChtFile.MAX_BYTES) as? Net.Got)?.bytes?.toString(Charsets.UTF_8)

    private fun lines(text: String) = text.lines().filter { it.isNotEmpty() && !it.startsWith("#") }

    /** A kept file: its time (the first line, `#time\t<ms>`) and its text; null if missing or unreadable. */
    private fun readKept(file: File): Pair<Long, String>? {
        val text = try {
            file.readText()
        } catch (e: Exception) {
            return null
        }
        val t = text.substringBefore('\n').removePrefix("#time\t").toLongOrNull() ?: return null
        return t to text
    }

    private fun writeKept(file: File, time: Long, body: String) {
        try {
            dir.mkdirs()
            val tmp = File(file.path + ".tmp")
            tmp.writeText("#time\t$time\n$body")
            if (!tmp.renameTo(file)) {
                file.delete()
                tmp.renameTo(file)
            }
        } catch (e: Exception) {
            // not kept: downloaded again next time
        }
    }

    companion object {
        private const val MIB = 1 shl 20
        const val MONTH = 30L * 24 * 60 * 60 * 1000
        const val MAX_LIST = 5 * MIB
        const val MAX_PICTURE = 5 * MIB
        const val MAX_LISTING = 2 * MIB
        private const val SYSTEM = "Nintendo%20-%20Game%20Boy%20Advance"
        const val NAMES_URL = "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/$SYSTEM.dat"
        const val BOX_URL = "https://thumbnails.libretro.com/$SYSTEM/Named_Boxarts/"
        const val CHEAT_LIST_URL = "https://api.github.com/repos/libretro/libretro-database/contents/cht/$SYSTEM"
        const val CHEAT_URL = "https://raw.githubusercontent.com/libretro/libretro-database/master/cht/$SYSTEM/"

        /** A file name as one part of a URL path (spaces as %20, not +). */
        fun encode(part: String): String = URLEncoder.encode(part, "UTF-8").replace("+", "%20")
    }
}
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3`, then the Kotlin count command.
Expected: `BUILD SUCCESSFUL`; `152 0`.

- [x] **Step 5: Docs and commit**

`docs/PROGRESS.md` (Completed "M10b Task 5", the task table, Next → Task 6, Last updated); this plan's boxes ticked; check for stale lines.

```bash
git add app/src/main/java/com/tengen/gbaemu/BoxArtHelper.kt app/src/test/java/com/tengen/gbaemu/BoxArtHelperTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10b-box-art.md
git commit -m "feat: look up box art and cheat files from libretro (BoxArtHelper)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Shrinking a picture (`BoxPicture`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/BoxPicture.kt`
- Test: `app/src/androidTest/java/com/tengen/gbaemu/BoxPictureTest.kt` (new, 2 on-device tests)

- [x] **Step 1: Write the failing test**

```kotlin
package com.tengen.gbaemu

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Color
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.ByteArrayOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Box art is shrunk to fit 400 × 400 with its shape kept, as a JPEG (10b). */
@RunWith(AndroidJUnit4::class)
class BoxPictureTest {
    private fun png(w: Int, h: Int): ByteArray {
        val b = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888).apply { eraseColor(Color.RED) }
        return ByteArrayOutputStream().also { b.compress(Bitmap.CompressFormat.PNG, 100, it) }.toByteArray()
    }

    @Test
    fun aBigPictureIsShrunkToFitKeepingItsShape() {
        val jpeg = BoxPicture.shrink(png(1000, 800))!!
        assertEquals(0xFF.toByte(), jpeg[0]) // JPEG starts FF D8
        assertEquals(0xD8.toByte(), jpeg[1])
        val b = BitmapFactory.decodeByteArray(jpeg, 0, jpeg.size)
        assertEquals(400, b.width)
        assertEquals(320, b.height)
        val c = b.getPixel(200, 160)
        assertTrue(Color.red(c) > 240 && Color.green(c) < 20 && Color.blue(c) < 20)
    }

    @Test
    fun aSmallPictureKeepsItsSizeAndNonsenseIsRefused() {
        val b = BoxPicture.shrink(png(200, 100))!!.let { BitmapFactory.decodeByteArray(it, 0, it.size) }
        assertEquals(200, b.width)
        assertEquals(100, b.height)
        assertNull(BoxPicture.shrink(ByteArray(10)))
    }
}
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh androidtest 2>&1 | grep -E "^e:|BUILD" | head -3`
Expected: FAIL — `Unresolved reference 'BoxPicture'`.

- [x] **Step 3: Write `BoxPicture.kt`**

```kotlin
package com.tengen.gbaemu

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.ByteArrayOutputStream

/** Box art pictures (10b): any picture shrunk to fit [SIZE] × [SIZE] (its shape kept), as a JPEG. */
object BoxPicture {
    const val SIZE = 400

    /** [bytes] (PNG, JPEG, …) as a JPEG no bigger than [SIZE] × [SIZE]; null if Android can't read the picture. */
    fun shrink(bytes: ByteArray): ByteArray? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
        var sample = 1 // decoded no bigger than needed: the longer side stays at least SIZE
        while (maxOf(bounds.outWidth, bounds.outHeight) / (sample * 2) >= SIZE) sample *= 2
        val full = BitmapFactory.decodeByteArray(bytes, 0, bytes.size, BitmapFactory.Options().apply { inSampleSize = sample })
            ?: return null
        val scale = minOf(1f, SIZE.toFloat() / maxOf(full.width, full.height))
        val w = maxOf(1, Math.round(full.width * scale))
        val h = maxOf(1, Math.round(full.height * scale))
        val small = if (w == full.width && h == full.height) full else Bitmap.createScaledBitmap(full, w, h, true)
        return ByteArrayOutputStream().also { small.compress(Bitmap.CompressFormat.JPEG, 90, it) }.toByteArray()
    }
}
```

- [x] **Step 4: Run the tests**

Run: `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | tail -3`, then the on-device count command; `bash tools/dev.sh jvmtest 2>&1 | tail -2` (`UiTextTest`).
Expected: on-device `66`, 0 failures; Kotlin `152 0`.

- [x] **Step 5: Docs and commit**

`docs/PROGRESS.md` (Completed "M10b Task 6", the task table, Next → Task 7, Last updated); this plan's boxes ticked; check for stale lines.

```bash
git add app/src/main/java/com/tengen/gbaemu/BoxPicture.kt app/src/androidTest/java/com/tengen/gbaemu/BoxPictureTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10b-box-art.md
git commit -m "feat: shrink box art to card size (BoxPicture)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: The screens — square cards, the hold menu, the switch, Find cheats online

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Settings.kt` (`boxArt`), `SettingsScreens.kt` (the switch on the main page), `Screens.kt` (`LibraryScreen`, `GameCard`, `GamePicture`, `GameHoldMenu`, `CheatPanel`, new `OnlineCheats` / `OnlineCheatsDialog`)
- Test: `app/src/test/java/com/tengen/gbaemu/SettingsTest.kt` (2 tests extended); `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt` (+4 tests)

- [x] **Step 1: Write the failing tests**

`SettingsTest`: in `nothingStoredMeansTheDefaultsWhichAreTodaysBehaviour` add `assertEquals(true, s.boxArt)`; in `everySettingIsSavedAndReadBack` add `boxArt = false` to the `Settings(...)` list.

`ScreensTest` (inside the class; add `import android.graphics.Bitmap`, `import androidx.compose.ui.test.assertCountEquals` and `import androidx.compose.ui.test.onAllNodesWithContentDescription`):

```kotlin
    @Test
    fun smallCardsShowTheirBoxAndTheContinueCardDoesNot() {
        val box = Bitmap.createBitmap(40, 40, Bitmap.Config.ARGB_8888)
        val rows = listOf(GameEntry("a", "u:a", "game C", "aaaaaaaa", lastPlayed = 5), GameEntry("b", "u:b", "game D", "bbbbbbbb"),
            GameEntry("c", "u:c", "Homebrew", "cccccccc"))
        compose.setContent {
            AppTheme {
                LibraryScreen(rows, "GBA", scanning = false, message = null, refresh = 0, picture = { null },
                    box = { g -> if (g.key == "c") null else box }, onPlay = {}, onHold = {}, onOpenGame = {},
                    onChangeFolder = {}, onSettings = {})
            }
        }
        compose.onNodeWithText("game D").assertIsDisplayed()
        compose.onAllNodesWithContentDescription("Box art").assertCountEquals(1) // game D only
    }

    @Test
    fun theHoldMenuOffersChooseBoxArtAndRemoveOnlyWithABox() {
        var chosen = 0
        var removed = 0
        val hasBox = mutableStateOf(false)
        compose.setContent {
            AppTheme {
                GameHoldMenu(GameEntry("a", "u:a", "game C"), onStartFresh = {}, onImport = {}, onExport = {},
                    onGameSettings = {}, onRemove = null, onClose = {}, hasBox = hasBox.value,
                    onChooseBox = { chosen++ }, onRemoveBox = { removed++ })
            }
        }
        compose.onNodeWithText("Remove box art").assertDoesNotExist()
        compose.onNodeWithText("Choose box art").performClick()
        assertEquals(1, chosen)
        hasBox.value = true
        compose.onNodeWithText("Remove box art").performClick()
        assertEquals(1, removed)
    }

    @Test
    fun theDownloadBoxArtSwitchIsOnTheMainSettingsPage() {
        var changed = Settings()
        compose.setContent {
            AppTheme {
                SettingsScreen(SettingsPage.MAIN, Settings(), KeyBindings(MemoryStore()), 0, "Built-in",
                    onPage = {}, onBack = {}, onChange = { changed = it }, onPickKey = { _, _ -> }, onResetKeys = {},
                    onChooseBios = {}, onBuiltInBios = {})
            }
        }
        compose.onNodeWithText("Download box art").performScrollTo().performClick()
        assertEquals(false, changed.boxArt)
    }

    @Test
    fun findCheatsOnlineShowsLookingTheFilesOrAMessage() {
        var asked = 0
        var picked = ""
        val state = mutableStateOf<OnlineCheats?>(null)
        compose.setContent {
            AppTheme {
                CheatPanel("game C", emptyList(), onToggle = { _, _ -> }, onEdit = {}, onAdd = {}, onBack = {},
                    onFindOnline = { asked++ })
                state.value?.let { OnlineCheatsDialog(it, onPick = { f -> picked = f }, onClose = { state.value = null }) }
            }
        }
        compose.onNodeWithText("Find cheats online").performClick()
        assertEquals(1, asked)
        state.value = OnlineCheats.Looking
        compose.onNodeWithText("Looking for cheat files").assertIsDisplayed()
        state.value = OnlineCheats.Found(listOf("Code Breaker" to "game C (USA) (Code Breaker).cht", "GameShark" to "g.cht"))
        compose.onNodeWithText("Code Breaker").performClick()
        assertEquals("game C (USA) (Code Breaker).cht", picked)
        state.value = OnlineCheats.Message(CheatSources.NONE_FOUND)
        compose.onNodeWithText("No cheat files found for this game.").assertIsDisplayed()
        compose.onNodeWithText("Close").performClick()
        compose.onNodeWithText("Cheats online").assertDoesNotExist()
    }
```

- [x] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh jvmtest 2>&1 | grep -E "^e:|BUILD" | head -3`
Expected: FAIL — `No parameter with name 'boxArt'` / `Unresolved reference 'boxArt'`.

- [x] **Step 3: The setting**

`Settings.kt`: add the field after `skinImport` —

```kotlin
    val boxArt: Boolean = true, // box art downloaded for the game list (10b)
```

in `save`: `store.put("box_art", flag(boxArt))`; in `load`: `boxArt = bool(store, "box_art", d.boxArt),`.

`SettingsScreens.kt`, `SettingsPage.MAIN`, after the BIOS `NavRow`:

```kotlin
                        SwitchRow("Download box art", s.boxArt, { onChange(s.copy(boxArt = it)) },
                            help = "Pictures of game boxes from libretro, for the game list. Uses the internet.")
```

- [x] **Step 4: The square cards**

`Screens.kt`: `LibraryScreen` gets `box: (GameEntry) -> Bitmap? = { null },` after `picture`, passed to both `GameCard` calls (`GameCard(last, refresh, picture, box, onPlay, onHold, big = true)`, the same for the grid). `GameCard(game, refresh, picture, box, onPlay, onHold, big)` adds after `val bitmap = …`:

```kotlin
    val boxBitmap = remember(game.id, refresh) { if (big) null else box(game) }
```

and both `GamePicture(bitmap, …)` calls become `GamePicture(bitmap, boxBitmap, big, …)`. Replace `GamePicture` with:

```kotlin
/**
 * A game's picture. The big card: where it was left (3:2). A small card (10b): a square with the game's box if it has
 * one, else the picture of where it was left or the placeholder, centred.
 */
@Composable
private fun GamePicture(bitmap: Bitmap?, box: Bitmap?, big: Boolean, modifier: Modifier) {
    if (big) {
        LeftPicture(bitmap, big, modifier)
        return
    }
    Box(modifier.aspectRatio(1f).clip(RoundedCornerShape(8.dp)).background(MaterialTheme.colorScheme.surfaceContainerHighest),
        contentAlignment = Alignment.Center) {
        if (box != null) {
            Image(box.asImageBitmap(), contentDescription = "Box art", modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Fit)
        } else {
            LeftPicture(bitmap, big, Modifier.fillMaxWidth())
        }
    }
}

/** The picture of where the game was left (3:2, sharp pixels), or a placeholder. */
@Composable
private fun LeftPicture(bitmap: Bitmap?, big: Boolean, modifier: Modifier) {
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
```

- [x] **Step 5: The hold menu**

`GameHoldMenu` gets `hasBox: Boolean = false, onChooseBox: () -> Unit = {}, onRemoveBox: () -> Unit = {}` after `onClose`, and after the "Game settings" item:

```kotlin
        SheetItem(Icons.Filled.Image, "Choose box art", onClick = onChooseBox),
        if (hasBox) SheetItem(Icons.Filled.HideImage, "Remove box art", onClick = onRemoveBox) else null,
```

(imports `androidx.compose.material.icons.filled.Image`, `androidx.compose.material.icons.filled.HideImage`.)

- [x] **Step 6: Find cheats online**

`CheatPanel` gets `onFindOnline: () -> Unit = {}` after `onRemoveAll`; after the Import cheat file button:

```kotlin
                Spacer(Modifier.height(8.dp))
                OutlinedButton(onClick = onFindOnline, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Icon(Icons.Filled.CloudDownload, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Find cheats online")
                }
```

After `CheatPanel`:

```kotlin
/** What Find cheats online shows (10b): looking, the files found as (label, file), or a message. */
sealed interface OnlineCheats {
    data object Looking : OnlineCheats
    data class Found(val files: List<Pair<String, String>>) : OnlineCheats
    data class Message(val text: String) : OnlineCheats
}

/** The Cheats online pop-up: a tap on a file picks it. */
@Composable
fun OnlineCheatsDialog(state: OnlineCheats, onPick: (String) -> Unit, onClose: () -> Unit) {
    AlertDialog(
        onDismissRequest = onClose,
        title = { Text("Cheats online") },
        text = {
            when (state) {
                OnlineCheats.Looking -> Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(Modifier.size(24.dp))
                    Spacer(Modifier.width(12.dp))
                    Text("Looking for cheat files")
                }
                is OnlineCheats.Found -> Column(Modifier.verticalScroll(rememberScrollState())) {
                    for ((label, file) in state.files) {
                        Text(label, Modifier.fillMaxWidth().clickable { onPick(file) }.padding(vertical = 14.dp))
                    }
                }
                is OnlineCheats.Message -> Text(state.text)
            }
        },
        confirmButton = {},
        dismissButton = { TextButton(onClick = onClose) { Text("Close") } },
    )
}
```

(import `androidx.compose.material.icons.filled.CloudDownload`.)

- [x] **Step 7: Run the tests**

Run: `bash tools/dev.sh jvmtest 2>&1 | tail -3` and the Kotlin count; `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | tail -3` and the on-device count.
Expected: Kotlin `152 0`; on-device `70`, 0 failures. (`MainActivity` still builds: every new parameter has a default.)

- [x] **Step 8: Docs and commit**

`docs/PROGRESS.md` (Completed "M10b Task 7", the task table, Next → Task 8, Last updated); this plan's boxes ticked; check for stale lines.

```bash
git add app/src/main/java/com/tengen/gbaemu/Settings.kt app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/test/java/com/tengen/gbaemu/SettingsTest.kt app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10b-box-art.md
git commit -m "feat: square cards with box art, Choose/Remove box art, the switch, Find cheats online

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: The wiring (`MainActivity`)

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`

No new test: the pieces are tested in Tasks 1–7; the wiring is checked by Claude on the emulator (Task 9).

- [x] **Step 1: State and the helper**

Fields (with the others):

```kotlin
    private lateinit var boxArt: BoxArt // the game list's box art (10b)
    private var boxHelper: BoxArtHelper? = null // looking up box art while the game list shows
    private var boxFor: GameEntry? = null // the game whose box is being chosen
    private var currentCode = "" // the running game's header code (for Find cheats online)
    private var onlineCheats by mutableStateOf<OnlineCheats?>(null)
```

a picker after `pickCht`:

```kotlin
    private val pickBox = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) boxChosen(uri)
    }
```

in `onCreate`, after `library = Library(filesDir)`: `boxArt = BoxArt(File(filesDir, "boxart"))`.

New functions (after `refreshLibrary`):

```kotlin
    private fun newBoxHelper() = BoxArtHelper(File(filesDir, "boxart"), boxArt, Net::get,
        { g -> readGame(Uri.parse(g.uri)) }, BoxPicture::shrink)

    /** Box art for the games without one, on a background thread, while the game list shows (and the setting is on). */
    private fun startBoxArt() {
        if (!settings.boxArt || playing || boxHelper != null) return
        val h = newBoxHelper()
        boxHelper = h
        val games = library.rows()
        thread(name = "box-art") {
            h.run(games,
                onId = { g, id ->
                    runOnUiThread {
                        library.games.find { it.key == g.key && it.id.isEmpty() }?.let { library.played(it.copy(id = id)) }
                        library.save()
                        rows = library.rows()
                    }
                },
                onPicture = { runOnUiThread { libraryRefresh++ } })
            runOnUiThread { if (boxHelper === h) boxHelper = null }
        }
    }

    private fun stopBoxArt() {
        boxHelper?.stop()
        boxHelper = null
    }

    /** The game's box, if it has one (small cards only). */
    private fun boxOf(g: GameEntry): Bitmap? =
        if (g.id.isEmpty()) null else boxArt.picture(g.id)?.let { BitmapFactory.decodeFile(it.path) }

    /** A picture the user picked as [boxFor]'s box. */
    private fun boxChosen(uri: Uri) {
        val g = boxFor ?: return
        boxFor = null
        val id = idOf(g) ?: return
        val jpeg = readUpTo(uri, 50 shl 20)?.let(BoxPicture::shrink)
        if (jpeg == null || !boxArt.own(id, jpeg)) {
            toast("That picture couldn't be read.")
            return
        }
        libraryRefresh++
    }
```

- [x] **Step 2: Starting and stopping**

- `refreshLibrary()`: on its early return, `startBoxArt()` before `return` (`if (…) { startBoxArt(); return }`); at the end of its `runOnUiThread` block (after `libraryRefresh++`): `startBoxArt()`.
- `open(entry, …)`: first line `stopBoxArt()`; after `val gameId = GameId.of(bytes)`: `currentCode = GameNames.code(bytes)`.
- `onPause()`: `stopBoxArt()` after `super.onPause()`.
- `changeSettings(s)`: at the end, `if (s.boxArt) { if (!playing) startBoxArt() } else stopBoxArt()`.

- [x] **Step 3: The screens**

- `LibraryScreen(…)`: add `box = ::boxOf,` after `picture = …`.
- `GameHoldMenu(g, …)`: add

```kotlin
                        hasBox = g.id.isNotEmpty() && boxArt.picture(g.id) != null,
                        onChooseBox = { holdGame = null; boxFor = g; pickBox.launch(arrayOf("image/*")) },
                        onRemoveBox = {
                            holdGame = null
                            idOf(g)?.let { boxArt.remove(it) }
                            libraryRefresh++
                        },
```

- `CheatPanel(…)`: add `onFindOnline = ::findCheatsOnline`.
- After the `deleteCheat?.let { … }` block:

```kotlin
                onlineCheats?.let { OnlineCheatsDialog(it, onPick = ::pickOnlineCheats, onClose = { onlineCheats = null }) }
```

- [x] **Step 4: Find cheats online, and 10a's import from text**

Split 10a's `importCheats(uri)` so the text path is shared:

```kotlin
    /** Adds a picked `.cht` file's cheats to the game's list, switched off; a note says what happened (10a). */
    private fun importCheats(uri: Uri) = importCheatText(readUpTo(uri, ChtFile.MAX_BYTES)?.toString(Charsets.UTF_8))

    /** A `.cht` file's text (null: unreadable) into the game's list, switched off, with the note (10a, 10b). */
    private fun importCheatText(text: String?) {
        val found = text?.let(ChtFile::parse).orEmpty()
        if (found.isEmpty()) {
            toast(ChtFile.NO_CHEATS)
            return
        }
        val m = ChtFile.merge(cheats, found) { NativeBridge.checkCheat(it, NativeBridge.CHEAT_AUTO)[1] == 0 }
        if (m.added > 0) changeCheats(m.cheats)
        toast(ChtFile.note(m))
    }

    /** Find cheats online: the game's name (remembered with its box art, or looked up), then libretro's files for it. */
    private fun findCheatsOnline() {
        val g = current ?: return
        val code = currentCode
        onlineCheats = OnlineCheats.Looking
        thread(name = "cheats-online") {
            val h = newBoxHelper()
            val known = boxArt.entry(g.id)?.name?.ifEmpty { null }
            val names = if (known == null) h.names() else null
            val name = known ?: names?.find(g.id, code)
            val state = when {
                name == null -> OnlineCheats.Message(if (names == null) CheatSources.NO_INTERNET else CheatSources.NONE_FOUND)
                else -> when (val files = h.cheatFiles(name)) {
                    null -> OnlineCheats.Message(CheatSources.NO_INTERNET)
                    else -> if (files.isEmpty()) OnlineCheats.Message(CheatSources.NONE_FOUND) else OnlineCheats.Found(files)
                }
            }
            runOnUiThread { if (onlineCheats != null) onlineCheats = state } // not if closed meanwhile
        }
    }

    /** One of libretro's cheat files, downloaded and imported as 10a's Import cheat file. */
    private fun pickOnlineCheats(file: String) {
        onlineCheats = OnlineCheats.Looking
        thread(name = "cheats-online") {
            val text = newBoxHelper().cheatFile(file)
            runOnUiThread {
                if (onlineCheats == null) return@runOnUiThread // closed meanwhile
                onlineCheats = null
                if (text == null) toast(CheatSources.NO_INTERNET) else importCheatText(text)
            }
        }
    }
```

- [x] **Step 5: Build and run the tests**

Run: `bash tools/dev.sh apk 2>&1 | grep -E "^e:|BUILD"`, then `bash tools/dev.sh jvmtest 2>&1 | tail -2` and the Kotlin count.
Expected: `BUILD SUCCESSFUL` both; Kotlin `152 0`.

- [x] **Step 6: Docs and commit**

`docs/PROGRESS.md` (Completed "M10b Task 8", the task table, Next → Task 9, Last updated); this plan's boxes ticked; check for stale lines.

```bash
git add app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10b-box-art.md
git commit -m "feat: box art and online cheats in the app

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Claude's check, milestone 10b exit

**Files:** docs only (`docs/PROGRESS.md`, this plan, the spec's status, the parent spec's status line and §10 row 10, `CLAUDE.md` key files).

- [x] **Step 1: Full runs**

```bash
bash tools/dev.sh test 2>&1 | tail -3
bash tools/dev.sh jvmtest 2>&1 | tail -3
rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest 2>&1 | tail -3
```

Expected: CTest `100% tests passed, 0 tests failed out of 120`; Kotlin `152 0`; on-device 70, 0 failures.

- [x] **Step 2: Claude's check on the emulator** (real internet; covered by the user's design approval). `bash tools/dev.sh install`. Back up first into the scratchpad: `files/cheats/*.json`, `files/library.json`, `shared_prefs/settings.xml` (with `adb exec-out run-as com.tengen.gbaemu cat …`); put them back at the end (the app closed first: `adb shell am force-stop com.tengen.gbaemu`) and delete `files/boxart` (`run-as com.tengen.gbaemu rm -r files/boxart`) unless the user wants to keep the boxes (ask in the report). With `uiautomator dump` taps and screenshots:
  1. hack 1 is the Continue card (it keeps where it was left), so first open `m7_demo` once and quit: m7_demo becomes the Continue card and hack 1 a small card. On the game list, wait up to a minute. Expected: `files/boxart/names.tsv` (≈ 3700 rows: `run-as … wc -l`), `boxart.json` with hack 1's id → `found`, "game A - game A Version (USA, Europe)"; `m7_demo` / `obj_aff` → `none`; the small cards square, the box drawn whole.
  2. The Continue card unchanged (where it was left).
  3. Hold a game without a box → Choose box art → a picture (push a PNG made in the scratchpad with `bash tools/dev.sh push`) → its card shows it; `boxart.json` → `own`. Remove box art → back to today's picture, `removed`; relaunch: it doesn't come back.
  4. Settings → Download box art off; remove `files/boxart` (app closed), reopen: nothing downloaded (`files/boxart` absent or empty), no message; on again → the boxes come back.
  5. Airplane mode (`adb shell cmd connectivity airplane-mode enable`), `files/boxart` removed, reopen: no message, nothing written; airplane mode off, reopen the list → the boxes arrive.
  6. hack 1 → pause menu → Cheats → Find cheats online → "Code Breaker" → tap → "Added 72 cheats. 2 couldn't be read." (as 10a); Remove all; in airplane mode → "Couldn't reach the internet."; for `m7_demo` → "No cheat files found for this game."
  7. Put back: the cheats files, `settings.xml`, airplane mode off.
  Anything wrong → superpowers:systematic-debugging, fix test-first, commit, re-check.

- [x] **Step 3: Docs, tag and the merge question**

The spec's status → implemented (milestone 10b complete, Claude's check passed); the parent spec's status line and §10 row 10 → 10b ✅, 10c next; `CLAUDE.md` key files → current milestone 10c cloud sync (needs brainstorming), last finished: this spec and plan; `docs/PROGRESS.md` → Completed (each task's hash and notes, the check), Project status (10b ✅), Next (brainstorm 10c), Last updated; every box in this plan ticked. Check every document for anything stale. Commit (`docs: milestone 10b complete - box art`), `git tag milestone-10b`, then superpowers:finishing-a-development-branch (the user picks merge / PR / keep).
