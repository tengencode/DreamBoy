# Milestone 6a — Game Library — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** A game library: the user picks a games folder once; the app lists its `.gba` and `.zip` games (plus games opened from elsewhere) with the picture of where each was left and when it was last played; tap to play, hold for Start fresh / Import save / Export save / Remove from list.

**Architecture:** App code only (the core and the C platform layer do not change). `Library` keeps the list in `files/library.json` (with `Json`, the small JSON code moved out of `Cheats`); `FolderScan` lists the folder through the Storage Access Framework off the main thread; `RomFile` reads a game from a `.gba` or a `.zip`; `SaveFiles` exports and imports saves; `Screens.kt` gets the first-start screen, the list and the hold menu; `MainActivity` wires them in place of the old start screen and `LastGame`.

**Tech Stack:** Kotlin + Jetpack Compose, Android Storage Access Framework (DocumentsContract), java.util.zip, JUnit 4 + androidx.test.

**Spec:** `docs/superpowers/specs/2026-09-28-milestone-6a-library-design.md`

---

## Conventions (same as 5c)

- Git Bash from the repo root. PC: `bash tools/dev.sh <configure|build|unit [filter]|test [ctest args]|run>`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push <file>>` — Gradle `--offline`; a missing artifact → stop and ask the user before any download.
- **Only the phone emulator, never the user's phone**: `dev.sh` exports `ANDROID_SERIAL=<emulator>`; the helpers below refuse without an emulator. `androidtest` leaves the app and its saves installed. Never uninstall the app, `pm clear` it or delete its `files/saves` / `files/states` / `files/cheats` / `files/library.json` (except the test files named in a step, and the list only as the check in Task 4 says, which puts it back) without the user's OK. Starting a closed emulator (`dev.sh emu`) is fine; ask before restarting/cold-booting it (a cold boot: see PROGRESS.md Notes).
- One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — project rule in `CLAUDE.md`.
- Branch `milestone-6` (already created; the spec commits are on it). The tasks run one after another without stopping (`CLAUDE.md`); the only stop is Task 5, the user's check and the 6a exit. After every task: all documents updated and checked for anything stale.
- This plan's code is the prototype's (worktree `<scratchpad>/p6a`, branch `proto-6a`), which the user tried on 2026-09-28 ('Everything works').

## File map

```
app/src/main/java/com/tengen/gbaemu/Json.kt (new), Cheats.kt, Library.kt (new); app/src/test/java/com/tengen/gbaemu/LibraryTest.kt (new)                   Task 1
app/src/main/java/com/tengen/gbaemu/RomFile.kt (new); app/src/test/java/com/tengen/gbaemu/RomFileTest.kt (new)                                              Task 2
app/src/main/java/com/tengen/gbaemu/SaveFiles.kt (new); app/src/test/java/com/tengen/gbaemu/SaveFilesTest.kt (new); app/src/androidTest/java/com/tengen/gbaemu/LibraryDeviceTest.kt (new)     Task 3
app/src/main/java/com/tengen/gbaemu/FolderScan.kt (new), Screens.kt, MainActivity.kt; LastGame.kt deleted                       Task 4
```

---
### Task 1: The game list and its file

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/Json.kt`, `app/src/main/java/com/tengen/gbaemu/Library.kt`, `app/src/test/java/com/tengen/gbaemu/LibraryTest.kt`
- Modify: `app/src/main/java/com/tengen/gbaemu/Cheats.kt` (its JSON code moves to `Json.kt`; the file format is unchanged)

- [x] **Step 1: Write the failing tests**

`app/src/test/java/com/tengen/gbaemu/LibraryTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LibraryTest {
    private fun found(name: String) = FoundGame("docs/$name", "content://tree/$name", Library.nameOf(name))

    @Test
    fun namesComeFromTheFileName() {
        assertEquals("hack 1", Library.nameOf("hack-1.gba"))
        assertEquals("game L", Library.nameOf("game L.ZIP"))
        assertEquals("game C", Library.nameOf("game C.GbA"))
        assertEquals("notes.txt", Library.nameOf("notes.txt"))
        assertTrue(Library.isGameFile("a.GBA") && Library.isGameFile("b.zip"))
        assertFalse(Library.isGameFile("c.sav") || Library.isGameFile("gba"))
    }

    @Test
    fun playedGamesComeFirstNewestFirstThenTheRestAToZ() {
        val lib = Library(Files.createTempDirectory("lib").toFile())
        lib.merge(listOf(found("game P.gba"), found("game M.gba"), found("game L.zip"), found("b.gba")))
        lib.played(lib.games.first { it.name == "game L" }.copy(id = "11111111", lastPlayed = 100))
        lib.played(lib.games.first { it.name == "b" }.copy(id = "22222222", lastPlayed = 200))
        assertEquals(listOf("b", "game L", "game M", "game P"), lib.rows().map { it.name })
    }

    @Test
    fun theFolderDecidesExceptForGamesFromOutsideIt() {
        val lib = Library(Files.createTempDirectory("lib").toFile())
        lib.merge(listOf(found("a.gba"), found("b.gba")))
        lib.played(lib.games.first { it.name == "a" }.copy(id = "aaaaaaaa", lastPlayed = 5))
        lib.played(GameEntry("other/w.gba", "content://single/w.gba", "w", "wwwwwwww", 7, outside = true))
        lib.merge(listOf(found("a.gba"), found("c.gba"))) // b deleted, c added
        assertEquals(listOf("w", "a", "c"), lib.rows().map { it.name })
        val a = lib.rows().first { it.name == "a" }
        assertEquals("aaaaaaaa", a.id) // what the app learnt is kept
        assertEquals(5L, a.lastPlayed)
        assertTrue(lib.rows().first { it.name == "w" }.outside)
        // an outside game later found in the folder becomes one of the folder's
        lib.merge(listOf(found("a.gba"), found("c.gba"), FoundGame("other/w.gba", "content://tree/w.gba", "w")))
        val w = lib.rows().first { it.name == "w" }
        assertFalse(w.outside)
        assertEquals("content://tree/w.gba", w.uri)
        assertEquals("wwwwwwww", w.id)
        lib.remove("docs/c.gba")
        assertEquals(listOf("w", "a"), lib.rows().map { it.name })
    }

    @Test
    fun anOutsideGameFoundInTheFolderByNameAndSizeBecomesTheFoldersOne() {
        // the same file reached through "Downloads" (another provider, another key) and through the games folder
        val lib = Library(Files.createTempDirectory("lib").toFile())
        lib.played(GameEntry("downloads/msf:51", "content://downloads/51", "hack 1", "e86b3224", 9, outside = true, size = 33554432))
        lib.played(GameEntry("downloads/msf:52", "content://downloads/52", "m7_demo", "aaaaaaaa", 8, outside = true, size = 100))
        lib.merge(listOf(FoundGame("tree/GBA/hack 1.gba", "content://tree/hack 1.gba", "hack 1", 33554432),
            FoundGame("tree/GBA/m7_demo.gba", "content://tree/m7_demo.gba", "m7_demo", 999))) // another size: another game
        val rows = lib.rows()
        assertEquals(listOf("hack 1", "m7_demo", "m7_demo"), rows.map { it.name })
        assertEquals(GameEntry("tree/GBA/hack 1.gba", "content://tree/hack 1.gba", "hack 1", "e86b3224", 9, outside = false, size = 33554432), rows[0])
        assertTrue(rows[1].outside)
        assertFalse(rows[2].outside)
        assertEquals(0L, rows[2].lastPlayed)
    }

    @Test
    fun aTwinFoundAfterTheFolderCopyWasListedJoinsIt() {
        // the folder copy was already listed (never played) when the outside game's size became known
        val lib = Library(Files.createTempDirectory("lib").toFile())
        val copy = FoundGame("tree/GBA/hack 1.gba", "content://tree/hack 1.gba", "hack 1", 33554432)
        lib.merge(listOf(copy))
        lib.played(GameEntry("downloads/msf:51", "content://downloads/51", "hack 1", "e86b3224", 9, outside = true, size = 33554432))
        lib.merge(listOf(copy))
        assertEquals(listOf(GameEntry("tree/GBA/hack 1.gba", "content://tree/hack 1.gba", "hack 1", "e86b3224", 9, outside = false,
            size = 33554432)), lib.rows())
    }

    @Test
    fun theListIsSavedAndReadBack() {
        val dir = Files.createTempDirectory("lib").toFile()
        val lib = Library(dir)
        assertTrue(lib.load()) // no file yet: an empty list
        assertNull(lib.folder)
        lib.folder = "content://com.android.externalstorage.documents/tree/primary%3ADownload"
        lib.merge(listOf(found("game A \"game A\".gba"), found("x.zip")))
        lib.played(GameEntry("k\\2", "content://single/y", "y \n z", "0123abcd", 1790619545123, outside = true))
        assertTrue(lib.save())
        val back = Library(dir)
        assertTrue(back.load())
        assertEquals(lib.folder, back.folder)
        assertEquals(lib.rows(), back.rows())
        dir.deleteRecursively()
    }

    @Test
    fun aDamagedFileIsKeptAsBadAndTheListStartsEmpty() {
        val dir = Files.createTempDirectory("lib").toFile()
        File(dir, "library.json").writeText("{\"folder\": \"x\", \"games\": [{\"key\": ")
        val lib = Library(dir)
        assertFalse(lib.load())
        assertTrue(File(dir, "library.json.bad").isFile)
        assertFalse(File(dir, "library.json").exists())
        assertNull(lib.folder)
        assertEquals(emptyList<GameEntry>(), lib.rows())
        dir.deleteRecursively()
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — `Unresolved reference 'FoundGame'`, `'Library'`, `'GameEntry'`.

- [x] **Step 3: Write the code**

`app/src/main/java/com/tengen/gbaemu/Json.kt`:

```kotlin
package com.tengen.gbaemu

/**
 * The small JSON the app writes (cheats, the game list): strings, whole numbers, true/false, lists and objects of
 * those. By hand, so it is tested on the PC without Android's org.json.
 */
object Json {
    /** [s] as a JSON string, quotes included. */
    fun str(s: String): String = buildString {
        append('"')
        for (c in s) when {
            c == '"' -> append("\\\"")
            c == '\\' -> append("\\\\")
            c == '\n' -> append("\\n")
            c == '\r' -> append("\\r")
            c == '\t' -> append("\\t")
            c < ' ' -> append("\\u%04x".format(c.code))
            else -> append(c)
        }
        append('"')
    }

    /** Reads JSON piece by piece; every call throws on anything unexpected. */
    class Reader(private val s: String) {
        private var i = 0

        /** The next character after any spaces, or null at the end. */
        fun peek(): Char? {
            while (i < s.length && s[i].isWhitespace()) i++
            return if (i < s.length) s[i] else null
        }

        fun expect(c: Char) {
            if (peek() != c) error("expected $c at $i")
            i++
        }

        /** The next character, which must be [a] or [b]. */
        fun take(a: Char, b: Char): Char {
            val c = peek()
            if (c != a && c != b) error("expected $a or $b at $i")
            i++
            return c!!
        }

        fun string(): String {
            expect('"')
            val out = StringBuilder()
            while (true) {
                if (i >= s.length) error("unfinished string")
                val c = s[i++]
                when (c) {
                    '"' -> return out.toString()
                    '\\' -> {
                        if (i >= s.length) error("unfinished escape")
                        when (val e = s[i++]) {
                            '"', '\\', '/' -> out.append(e)
                            'n' -> out.append('\n')
                            'r' -> out.append('\r')
                            't' -> out.append('\t')
                            'b' -> out.append('\b')
                            'f' -> out.append('\u000C')
                            'u' -> {
                                if (i + 4 > s.length) error("short \\u")
                                out.append(s.substring(i, i + 4).toInt(16).toChar())
                                i += 4
                            }
                            else -> error("bad escape \\$e")
                        }
                    }
                    else -> out.append(c)
                }
            }
        }

        fun number(): Long {
            peek()
            val start = i
            if (i < s.length && s[i] == '-') i++
            while (i < s.length && s[i].isDigit()) i++
            return s.substring(start, i).toLongOrNull() ?: error("expected a number at $start")
        }

        fun bool(): Boolean = when {
            peek() == 't' && s.startsWith("true", i) -> { i += 4; true }
            peek() == 'f' && s.startsWith("false", i) -> { i += 5; false }
            else -> error("expected true or false at $i")
        }

        /** `null` (then true) or nothing read (false). */
        fun nullValue(): Boolean = if (peek() == 'n' && s.startsWith("null", i)) { i += 4; true } else false

        /** Calls [item] for each element of a list `[...]` (empty lists allowed). */
        fun list(item: () -> Unit) {
            expect('[')
            if (peek() == ']') { i++; return }
            do item() while (take(',', ']') == ',')
        }

        /** Calls [field] with each key of an object `{...}`, the reader placed on its value. */
        fun obj(field: (String) -> Unit) {
            expect('{')
            if (peek() == '}') { i++; return }
            do {
                val key = string()
                expect(':')
                field(key)
            } while (take(',', '}') == ',')
        }

        /** Throws if anything but spaces is left. */
        fun end() {
            if (peek() != null) error("text after the end at $i")
        }
    }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/Cheats.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File

/** One cheat: its name, the type picked (NativeBridge.CHEAT_*), its codes as typed, and whether it is switched on. */
data class Cheat(val name: String, val type: Int, val codes: String, val on: Boolean)

/**
 * A game's cheats in `files/cheats/<game id>.json`: a list of {name, type, codes, on}. Written safely (a temporary
 * file, then swapped), with [Json].
 */
class Cheats(filesDir: File, gameId: String) {
    private val file = File(filesDir, "cheats/$gameId.json")

    /** The cheats; empty without a file; null if the file is damaged (it is then kept as `.json.bad`). */
    fun load(): List<Cheat>? {
        if (!file.isFile) return emptyList()
        return try {
            fromJson(file.readText())
        } catch (e: Exception) {
            file.renameTo(File(file.path + ".bad"))
            null
        }
    }

    fun save(cheats: List<Cheat>): Boolean = try {
        file.parentFile?.mkdirs()
        val tmp = File(file.path + ".tmp")
        tmp.writeText(toJson(cheats))
        if (!tmp.renameTo(file)) {
            file.delete()
            tmp.renameTo(file)
        } else true
    } catch (e: Exception) {
        false
    }

    companion object {
        fun toJson(cheats: List<Cheat>): String = cheats.joinToString(",\n", "[\n", "\n]\n") {
            "  {\"name\": ${Json.str(it.name)}, \"type\": ${it.type}, \"codes\": ${Json.str(it.codes)}, \"on\": ${it.on}}"
        }

        /** Reads what [toJson] writes (and any JSON of that shape); throws on anything else. */
        fun fromJson(json: String): List<Cheat> = Json.Reader(json).run {
            val cheats = mutableListOf<Cheat>()
            list {
                var name = ""
                var type = 0
                var codes = ""
                var on = false
                obj { key ->
                    when (key) {
                        "name" -> name = string()
                        "type" -> type = number().toInt()
                        "codes" -> codes = string()
                        "on" -> on = bool()
                        else -> error("unknown key $key")
                    }
                }
                cheats += Cheat(name, type, codes, on)
            }
            end()
            cheats
        }
    }
}
```

`app/src/main/java/com/tengen/gbaemu/Library.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File

/**
 * One game in the list. [key] names the file whichever way it was reached (the provider and its document id: the
 * same for the folder's copy of a file and for the file picked on its own); [uri] is how it is read. [id] (the
 * CRC-32 of its bytes, the key of its saves and states) is empty until the game is opened; [lastPlayed] 0 until then.
 * [outside]: opened with "Open a game…" from outside the games folder; it stays until removed. [size]: the file's
 * size in bytes (0 = unknown).
 */
data class GameEntry(
    val key: String,
    val uri: String,
    val name: String,
    val id: String = "",
    val lastPlayed: Long = 0,
    val outside: Boolean = false,
    val size: Long = 0,
)

/** A game file found in the games folder now ([size] in bytes, 0 = unknown). */
data class FoundGame(val key: String, val uri: String, val name: String, val size: Long = 0)

/**
 * The game list in `files/library.json`: the games folder (its tree URI) and what the app knows of each game.
 * The folder's files decide which in-folder games are listed ([merge]); outside games stay until [remove]d.
 */
class Library(filesDir: File) {
    private val file = File(filesDir, "library.json")
    var folder: String? = null
    var games: List<GameEntry> = emptyList()
        private set

    /** False: the file was damaged (kept as `library.json.bad`) and the list starts empty. No file = empty, true. */
    fun load(): Boolean {
        if (!file.isFile) return true
        return try {
            val (f, g) = fromJson(file.readText())
            folder = f
            games = g
            true
        } catch (e: Exception) {
            file.renameTo(File(file.path + ".bad"))
            folder = null
            games = emptyList()
            false
        }
    }

    fun save(): Boolean = try {
        val tmp = File(file.path + ".tmp")
        tmp.writeText(toJson(folder, games))
        if (!tmp.renameTo(file)) {
            file.delete()
            tmp.renameTo(file)
        } else true
    } catch (e: Exception) {
        false
    }

    /**
     * The folder's files now: they replace the in-folder games (keeping what is known of each); outside ones stay.
     * An outside game with a folder file's name and size is that file reached another way (e.g. through
     * "Downloads", whose keys differ): it joins the folder's (the more recently played of the two decides the id and
     * time), also when the folder's copy was listed before.
     */
    fun merge(found: List<FoundGame>) {
        val known = games.associateBy { it.key }
        val keys = found.mapTo(HashSet()) { it.key }
        val outside = games.filter { it.outside && it.key !in keys }.toMutableList()
        val inFolder = found.map { f ->
            val own = known[f.key]
            val twin = outside.firstOrNull { it.name == f.name && it.size > 0 && it.size == f.size }?.also { outside.remove(it) }
            val base = listOfNotNull(own, twin).maxByOrNull { it.lastPlayed }
            base?.copy(key = f.key, uri = f.uri, name = f.name, outside = false, size = f.size,
                id = base.id.ifEmpty { own?.id?.ifEmpty { null } ?: twin?.id ?: "" })
                ?: GameEntry(f.key, f.uri, f.name, size = f.size)
        }
        games = inFolder + outside
    }

    /** A game was opened: [entry] (with its id and time) replaces the one with its key, or is added. */
    fun played(entry: GameEntry) {
        games = if (games.any { it.key == entry.key }) games.map { if (it.key == entry.key) entry else it } else games + entry
    }

    fun remove(key: String) {
        games = games.filter { it.key != key }
    }

    /** Played games newest first, then the rest A-Z. */
    fun rows(): List<GameEntry> =
        games.filter { it.lastPlayed > 0 }.sortedByDescending { it.lastPlayed } +
            games.filter { it.lastPlayed <= 0 }.sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER) { it.name })

    companion object {
        private val EXTENSIONS = listOf(".gba", ".zip")

        fun isGameFile(fileName: String): Boolean = EXTENSIONS.any { fileName.endsWith(it, ignoreCase = true) && fileName.length > it.length }

        /** The name shown: the file name without `.gba` / `.zip`. */
        fun nameOf(fileName: String): String =
            EXTENSIONS.firstOrNull { fileName.endsWith(it, ignoreCase = true) }?.let { fileName.dropLast(it.length) } ?: fileName

        fun toJson(folder: String?, games: List<GameEntry>): String = buildString {
            append("{\n  \"folder\": ").append(folder?.let(Json::str) ?: "null").append(",\n  \"games\": [")
            games.forEachIndexed { i, g ->
                append(if (i == 0) "\n" else ",\n")
                append("    {\"key\": ${Json.str(g.key)}, \"uri\": ${Json.str(g.uri)}, \"name\": ${Json.str(g.name)}, ")
                append("\"id\": ${Json.str(g.id)}, \"lastPlayed\": ${g.lastPlayed}, \"outside\": ${g.outside}, \"size\": ${g.size}}")
            }
            append(if (games.isEmpty()) "]\n}\n" else "\n  ]\n}\n")
        }

        /** Reads what [toJson] writes; throws on anything else. */
        fun fromJson(json: String): Pair<String?, List<GameEntry>> = Json.Reader(json).run {
            var folder: String? = null
            val games = mutableListOf<GameEntry>()
            obj { key ->
                when (key) {
                    "folder" -> folder = if (nullValue()) null else string()
                    "games" -> list {
                        var k = ""
                        var uri = ""
                        var name = ""
                        var id = ""
                        var last = 0L
                        var outside = false
                        var size = 0L
                        obj { field ->
                            when (field) {
                                "key" -> k = string()
                                "uri" -> uri = string()
                                "name" -> name = string()
                                "id" -> id = string()
                                "lastPlayed" -> last = number()
                                "outside" -> outside = bool()
                                "size" -> size = number()
                                else -> error("unknown key $field")
                            }
                        }
                        if (k.isEmpty() || uri.isEmpty()) error("a game without its file")
                        games += GameEntry(k, uri, name, id, last, outside, size)
                    }
                    else -> error("unknown key $key")
                }
            }
            end()
            folder to games
        }
    }
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```
Expected: `LibraryTest` 7 plus the earlier 38 (`CheatsTest` 3 still green on the shared JSON code) — **45 tests**, 0 failures.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Json.kt app/src/main/java/com/tengen/gbaemu/Cheats.kt app/src/main/java/com/tengen/gbaemu/Library.kt app/src/test/java/com/tengen/gbaemu/LibraryTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-6a-library.md
git commit -m "feat(android): the game list in files/library.json; JSON code shared with the cheats" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: Reading a game from a .gba or a .zip

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/RomFile.kt`, `app/src/test/java/com/tengen/gbaemu/RomFileTest.kt`

- [x] **Step 1: Write the failing tests**

`app/src/test/java/com/tengen/gbaemu/RomFileTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class RomFileTest {
    private val game = ByteArray(4096) { (it * 7).toByte() }
    private val other = ByteArray(1000) { (it * 3 + 1).toByte() }

    private fun zip(vararg files: Pair<String, ByteArray>): ByteArray {
        val out = ByteArrayOutputStream()
        ZipOutputStream(out).use { z ->
            for ((name, bytes) in files) {
                z.putNextEntry(ZipEntry(name))
                z.write(bytes)
                z.closeEntry()
            }
        }
        return out.toByteArray()
    }

    private fun read(bytes: ByteArray) = RomFile.read(ByteArrayInputStream(bytes))

    private fun assertNotAGame(bytes: ByteArray, why: String) {
        try {
            read(bytes)
            fail("read: $why")
        } catch (e: RomFile.NotAGame) {
        }
    }

    @Test
    fun aPlainGameIsReadAsItIs() = assertArrayEquals(game, read(game))

    @Test
    fun aZippedGameIsTheFirstGbaFileInside() {
        assertArrayEquals(game, read(zip("readme.txt" to other, "dir/" to ByteArray(0), "Game (E).GBA" to game, "b.gba" to other)))
        assertTrue(RomFile.zipHasGame(ByteArrayInputStream(zip("readme.txt" to other, "g.gba" to game))))
    }

    @Test
    fun aZipWithoutAGameIsRefused() {
        val z = zip("readme.txt" to other, "game.sav" to other)
        assertNotAGame(z, "a zip without a .gba")
        assertFalse(RomFile.zipHasGame(ByteArrayInputStream(z)))
        assertFalse(RomFile.zipHasGame(ByteArrayInputStream(game))) // not a zip at all
    }

    @Test
    fun over32MiBIsRefusedAndExactly32MiBIsRead() {
        assertNotAGame(ByteArray(RomFile.MAX_SIZE + 1), "33 MiB")
        assertNotAGame(zip("big.gba" to ByteArray(RomFile.MAX_SIZE + 1)), "33 MiB zipped")
        assertEquals(RomFile.MAX_SIZE, read(ByteArray(RomFile.MAX_SIZE)).size)
        assertNotAGame(ByteArray(0), "an empty file")
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — `Unresolved reference 'RomFile'`.

- [x] **Step 3: Write the code**

`app/src/main/java/com/tengen/gbaemu/RomFile.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.util.zip.ZipInputStream

/** A game's bytes from a `.gba` file, or from the first `.gba` inside a `.zip` (told apart by the zip's first bytes). */
object RomFile {
    const val MAX_SIZE = 32 * 1024 * 1024

    /** Not a game this app can run: empty, over 32 MiB, or a zip without a `.gba` inside. */
    class NotAGame(message: String) : Exception(message)

    /** Reads the game (closes nothing: the caller owns [input]). Throws [NotAGame], or an IOException if reading fails. */
    fun read(input: InputStream): ByteArray {
        val buf = BufferedInputStream(input)
        buf.mark(4)
        val head = ByteArray(4)
        var n = 0
        while (n < 4) {
            val r = buf.read(head, n, 4 - n)
            if (r < 0) break
            n += r
        }
        buf.reset()
        if (n == 4 && isZip(head)) {
            val zip = ZipInputStream(buf)
            while (true) {
                val e = zip.nextEntry ?: throw NotAGame("no .gba file inside this zip")
                if (!e.isDirectory && e.name.endsWith(".gba", ignoreCase = true)) return limited(zip)
            }
        }
        return limited(buf)
    }

    /** True if [input] is a zip holding a `.gba` file (reads only as far as that file's name). */
    fun zipHasGame(input: InputStream): Boolean = try {
        val zip = ZipInputStream(BufferedInputStream(input))
        generateSequence { zip.nextEntry }.any { !it.isDirectory && it.name.endsWith(".gba", ignoreCase = true) }
    } catch (e: Exception) {
        false
    }

    private fun isZip(head: ByteArray) = head[0] == 'P'.code.toByte() && head[1] == 'K'.code.toByte() &&
        head[2] == 3.toByte() && head[3] == 4.toByte()

    /** All of [input], refusing more than [MAX_SIZE] bytes (and nothing at all). */
    private fun limited(input: InputStream): ByteArray {
        val out = ByteArrayOutputStream()
        val chunk = ByteArray(64 * 1024)
        while (true) {
            val n = input.read(chunk)
            if (n < 0) break
            out.write(chunk, 0, n)
            if (out.size() > MAX_SIZE) throw NotAGame("over 32 MiB")
        }
        if (out.size() == 0) throw NotAGame("an empty file")
        return out.toByteArray()
    }
}
```

(No `InputStream.readNBytes`: it exists only from Android 13, and the app supports Android 8.)

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```
Expected: **49 tests** (45 + 4), 0 failures.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/RomFile.kt app/src/test/java/com/tengen/gbaemu/RomFileTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-6a-library.md
git commit -m "feat(android): read a game from a .gba or the first .gba inside a .zip" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: Save files in and out

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/SaveFiles.kt`, `app/src/test/java/com/tengen/gbaemu/SaveFilesTest.kt`, `app/src/androidTest/java/com/tengen/gbaemu/LibraryDeviceTest.kt`

- [x] **Step 1: Write the failing tests**

`app/src/test/java/com/tengen/gbaemu/SaveFilesTest.kt`:

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

class SaveFilesTest {
    private val id = "e86b3224"

    @Test
    fun onlyTheSizesASaveChipCanHave() {
        for (n in listOf(512, 8192, 32768, 65536, 131072)) assertTrue("$n", SaveFiles.validSize(n))
        for (n in listOf(0, 511, 1024, 32767, 131073, 262144)) assertFalse("$n", SaveFiles.validSize(n))
    }

    @Test
    fun exportGivesTheSaveOrNothing() {
        val dir = Files.createTempDirectory("saves").toFile()
        assertNull(SaveFiles.export(dir, id))
        val bytes = ByteArray(32768) { it.toByte() }
        File(dir, "saves").mkdirs()
        File(dir, "saves/$id.sav").writeBytes(bytes)
        assertArrayEquals(bytes, SaveFiles.export(dir, id))
        dir.deleteRecursively()
    }

    @Test
    fun importKeepsTheOldSaveAndSetsTheAutomaticStateAside() {
        val dir = Files.createTempDirectory("saves").toFile()
        val old = ByteArray(131072) { 1 }
        val new = ByteArray(131072) { 2 }
        File(dir, "saves").mkdirs()
        File(dir, "saves/$id.sav").writeBytes(old)
        val states = File(dir, "states/$id").apply { mkdirs() }
        File(states, "auto.state").writeText("state")
        File(states, "auto.png").writeText("picture")
        File(states, "slot1.state").writeText("slot")
        assertEquals(SaveFiles.Import.OK, SaveFiles.import(dir, id, new))
        assertArrayEquals(new, File(dir, "saves/$id.sav").readBytes())
        assertArrayEquals(old, File(dir, "saves/$id.sav.bak").readBytes())
        assertFalse(File(states, "auto.state").exists())
        assertEquals("state", File(states, "auto.state.old").readText())
        assertEquals("picture", File(states, "auto.png.old").readText())
        assertEquals("slot", File(states, "slot1.state").readText()) // the slots stay
        dir.deleteRecursively()
    }

    @Test
    fun aFirstImportNeedsNoBackup() {
        val dir = Files.createTempDirectory("saves").toFile()
        val new = ByteArray(512) { 9 }
        assertEquals(SaveFiles.Import.OK, SaveFiles.import(dir, id, new))
        assertArrayEquals(new, File(dir, "saves/$id.sav").readBytes())
        assertFalse(File(dir, "saves/$id.sav.bak").exists())
        dir.deleteRecursively()
    }

    @Test
    fun aWrongSizeChangesNothing() {
        val dir = Files.createTempDirectory("saves").toFile()
        val old = ByteArray(32768) { 1 }
        File(dir, "saves").mkdirs()
        File(dir, "saves/$id.sav").writeBytes(old)
        val auto = File(dir, "states/$id/auto.state").apply { parentFile!!.mkdirs(); writeText("state") }
        assertEquals(SaveFiles.Import.WRONG_SIZE, SaveFiles.import(dir, id, ByteArray(1000)))
        assertArrayEquals(old, File(dir, "saves/$id.sav").readBytes())
        assertFalse(File(dir, "saves/$id.sav.bak").exists())
        assertTrue(auto.exists())
        dir.deleteRecursively()
    }
}
```

`app/src/androidTest/java/com/tengen/gbaemu/LibraryDeviceTest.kt`:

```kotlin
package com.tengen.gbaemu

import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The library on the phone: a game read from a zip runs; a save exported and imported comes back byte for byte. */
@RunWith(AndroidJUnit4::class)
class LibraryDeviceTest {
    private val dir = File(InstrumentationRegistry.getInstrumentation().targetContext.filesDir, "test_library")

    @After
    fun stopSession() {
        NativeBridge.stop()
        dir.deleteRecursively()
    }

    private fun bytesOf(romPath: String): ByteArray = TestRoms.rom(romPath).let { b -> ByteArray(b.remaining()).also { b.get(it) } }

    private fun direct(bytes: ByteArray): ByteBuffer = ByteBuffer.allocateDirect(bytes.size).put(bytes).also { it.flip() }

    @Test
    fun aZippedGameRunsWithTheIdOfTheGameInside() {
        val game = bytesOf("tonc/m7_demo.gba")
        val zip = ByteArrayOutputStream().also { out ->
            ZipOutputStream(out).use { z ->
                z.putNextEntry(ZipEntry("readme.txt")); z.write("hello".toByteArray()); z.closeEntry()
                z.putNextEntry(ZipEntry("m7_demo.gba")); z.write(game); z.closeEntry()
            }
        }.toByteArray()
        val read = RomFile.read(ByteArrayInputStream(zip))
        assertArrayEquals(game, read)
        assertEquals(GameId.of(game), GameId.of(read))
        assertEquals(0, NativeBridge.loadRom(direct(read)))
        NativeBridge.start()
        Thread.sleep(300)
        assertTrue("no frames ran", NativeBridge.debugFrameCount() > 0)
    }

    @Test
    fun anExportedSaveImportsBackExactly() {
        val game = bytesOf("jsmolka/sram.gba")
        val id = GameId.of(game)
        val sav = SaveFiles.file(dir, id).apply { parentFile!!.mkdirs() }
        assertEquals(0, NativeBridge.loadRom(direct(game), sav.path))
        NativeBridge.start()
        val until = SystemClock.elapsedRealtime() + 2000
        while (!sav.exists() && SystemClock.elapsedRealtime() < until) Thread.sleep(20)
        NativeBridge.pause()
        NativeBridge.stop()
        val exported = SaveFiles.export(dir, id)
        assertNotNull("the game wrote no save", exported)
        val other = File(dir, "other")
        assertEquals(SaveFiles.Import.OK, SaveFiles.import(other, id, exported!!))
        assertArrayEquals(exported, SaveFiles.file(other, id).readBytes())
        // and the game takes it: loaded again with the imported file, its save RAM is those bytes
        assertEquals(0, NativeBridge.loadRom(direct(game), SaveFiles.file(other, id).path))
        assertArrayEquals(exported, NativeBridge.debugSaveRam())
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — `Unresolved reference 'SaveFiles'`.

- [x] **Step 3: Write the code**

`app/src/main/java/com/tengen/gbaemu/SaveFiles.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File

/**
 * A game's save file `files/saves/<id>.sav`, in and out of the app from the library's hold menu (no game running).
 * The save itself is written by the emulation thread while a game runs.
 */
object SaveFiles {
    private val SIZES = setOf(512, 8192, 32768, 65536, 131072) // EEPROM 512 B / 8 KiB, SRAM, Flash 64 / 128 KiB

    enum class Import { OK, WRONG_SIZE, FAILED }

    fun validSize(size: Int): Boolean = size in SIZES

    fun file(filesDir: File, id: String) = File(filesDir, "saves/$id.sav")

    /** The save's bytes, or null if the game has none yet. */
    fun export(filesDir: File, id: String): ByteArray? = file(filesDir, id).takeIf { it.isFile }?.readBytes()

    /**
     * [bytes] become the game's save: the old one kept as `.sav.bak`, the new one written safely. The automatic
     * state is set aside (`auto.state.old`, `auto.png.old`): a state carries the save RAM, so resuming it would
     * bring the old save back. The next start is from the title with this save.
     */
    fun import(filesDir: File, id: String, bytes: ByteArray): Import {
        if (!validSize(bytes.size)) return Import.WRONG_SIZE
        return try {
            val sav = file(filesDir, id)
            sav.parentFile?.mkdirs()
            val tmp = File(sav.path + ".tmp")
            tmp.writeBytes(bytes)
            if (sav.isFile) sav.copyTo(File(sav.path + ".bak"), overwrite = true)
            if (!tmp.renameTo(sav)) {
                sav.delete()
                if (!tmp.renameTo(sav)) return Import.FAILED
            }
            val states = States(filesDir, id)
            for (f in listOf(states.state(States.AUTO), states.picture(States.AUTO))) {
                if (f.exists()) {
                    val old = File(f.path + ".old")
                    old.delete()
                    f.renameTo(old)
                }
            }
            Import.OK
        } catch (e: Exception) {
            Import.FAILED
        }
    }
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```
Expected: **54 tests** (49 + 5), 0 failures.

```bash
for i in 1 2 3; do rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
```
Expected: `tests="47" failures="0"` three times (45 + 2).

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/SaveFiles.kt app/src/test/java/com/tengen/gbaemu/SaveFilesTest.kt app/src/androidTest/java/com/tengen/gbaemu/LibraryDeviceTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-6a-library.md
git commit -m "feat(android): export and import a game's save (old one kept, automatic state set aside)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-3) done** — report briefly and carry on (no stop).

---
### Task 4: The folder, the list, the hold menu, the wiring

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/FolderScan.kt`
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt` (the start screen → `FirstStartScreen`, `LibraryScreen`, `GameHoldMenu`, `ReplaceSaveDialog`), `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`
- Delete: `app/src/main/java/com/tengen/gbaemu/LastGame.kt` (its last game is carried into the list once)

(No new automated test: the parts are covered by Tasks 1-3; the whole is checked by Claude in Step 3 and by the user in Task 5.)

- [x] **Step 1: Write the code**

`app/src/main/java/com/tengen/gbaemu/FolderScan.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document

/**
 * The games in the games folder and its subfolders, through the Storage Access Framework (the folder's tree
 * permission is kept). Slow on big folders: call it off the main thread.
 */
object FolderScan {
    private const val MAX_DEPTH = 8

    /** How the library matches a file however it was reached: its provider and document id. */
    fun keyOf(uri: Uri): String = "${uri.authority}/${DocumentsContract.getDocumentId(uri)}"

    /** The folder's name as the Files app shows it, or null if the folder can't be opened. */
    fun folderName(resolver: ContentResolver, tree: Uri): String? = try {
        val doc = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
        resolver.query(doc, arrayOf(Document.COLUMN_DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        }
    } catch (e: Exception) {
        null
    }

    /** The `.gba` files and the `.zip` files holding one; null if the folder can't be opened (gone, no permission). */
    fun scan(resolver: ContentResolver, tree: Uri): List<FoundGame>? = try {
        val out = mutableListOf<FoundGame>()
        walk(resolver, tree, DocumentsContract.getTreeDocumentId(tree), 0, out, top = true)
        out
    } catch (e: Exception) {
        null
    }

    private fun walk(resolver: ContentResolver, tree: Uri, parent: String, depth: Int, out: MutableList<FoundGame>, top: Boolean) {
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent)
        val cols = arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
            Document.COLUMN_SIZE)
        val cursor = resolver.query(children, cols, null, null, null)
        if (cursor == null) {
            if (top) error("the folder can't be listed") // a subfolder that can't be listed is just skipped
            return
        }
        cursor.use {
            while (it.moveToNext()) {
                val id = it.getString(0) ?: continue
                val name = it.getString(1) ?: continue
                if (it.getString(2) == Document.MIME_TYPE_DIR) {
                    if (depth < MAX_DEPTH) walk(resolver, tree, id, depth + 1, out, top = false)
                    continue
                }
                if (!Library.isGameFile(name)) continue
                val uri = DocumentsContract.buildDocumentUriUsingTree(tree, id)
                if (name.endsWith(".zip", ignoreCase = true)) {
                    val ok = try {
                        resolver.openInputStream(uri)?.use(RomFile::zipHasGame) ?: false
                    } catch (e: Exception) {
                        false
                    }
                    if (!ok) continue
                }
                val size = if (it.isNull(3)) 0L else it.getLong(3)
                out += FoundGame(keyOf(uri), uri.toString(), Library.nameOf(name), size)
            }
        }
    }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/Screens.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Context
import java.util.Calendar
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.layout.systemBarsPadding
import android.graphics.Bitmap
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.text.DateFormat
import java.util.Date
import kotlin.math.roundToInt
import androidx.compose.ui.viewinterop.AndroidView

/** The very first start (no games folder yet) or a folder that can't be opened any more. */
@Composable
fun FirstStartScreen(folderLost: Boolean, message: String?, onChooseFolder: () -> Unit, onOpenGame: () -> Unit,
                     onControls: () -> Unit) {
    Surface(modifier = Modifier.fillMaxSize()) { /* the theme's background and text colours */
        Column(
            modifier = Modifier.fillMaxSize().systemBarsPadding().padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("GBA Emulator", style = MaterialTheme.typography.headlineMedium)
            Spacer(Modifier.height(16.dp))
            Text(
                if (folderLost) "Can't open the games folder any more — choose it again."
                else "Pick the folder where your games are. New games you put there show up by themselves.\n\n" +
                    "Android doesn't allow the Download folder itself: make a folder such as GBA (the picker's " +
                    "Create new folder) and put your games in it.",
                textAlign = TextAlign.Center,
                color = if (folderLost) MaterialTheme.colorScheme.error else Color.Unspecified,
            )
            Spacer(Modifier.height(24.dp))
            Button(onClick = onChooseFolder) { Text("Choose games folder") }
            Spacer(Modifier.height(12.dp))
            OutlinedButton(onClick = onOpenGame) { Text("Open a single game…") }
            Spacer(Modifier.height(12.dp))
            OutlinedButton(onClick = onControls) { Text("Controls") }
            if (message != null) {
                Spacer(Modifier.height(16.dp))
                Text(message, color = MaterialTheme.colorScheme.error, textAlign = TextAlign.Center)
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
 * The game list: one row per game (the picture of where it was left, its name, when it was last played). A tap
 * plays it, holding opens [onHold]'s menu. [refresh] changes whenever the pictures may have changed.
 */
@Composable
fun LibraryScreen(rows: List<GameEntry>, folderName: String?, scanning: Boolean, message: String?, refresh: Int,
                  picture: (GameEntry) -> Bitmap?, onPlay: (GameEntry) -> Unit, onHold: (GameEntry) -> Unit,
                  onOpenGame: () -> Unit, onChangeFolder: () -> Unit, onControls: () -> Unit) {
    var menuOpen by remember { mutableStateOf(false) }
    Surface(modifier = Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().systemBarsPadding()) {
            Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 4.dp, top = 8.dp, bottom = 8.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Text("Games", style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
                Box {
                    TextButton(onClick = { menuOpen = true }) { Text("⋮", fontSize = 22.sp) }
                    DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                        DropdownMenuItem(text = { Text("Open a game…") }, onClick = { menuOpen = false; onOpenGame() })
                        DropdownMenuItem(text = { Text("Change folder…") }, onClick = { menuOpen = false; onChangeFolder() })
                        DropdownMenuItem(text = { Text("Controls") }, onClick = { menuOpen = false; onControls() })
                    }
                }
            }
            if (message != null) {
                Text(message, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp))
            }
            if (rows.isEmpty()) {
                Column(Modifier.fillMaxSize().padding(24.dp), verticalArrangement = Arrangement.Center,
                    horizontalAlignment = Alignment.CenterHorizontally) {
                    if (scanning) Text("Looking for games…")
                    else {
                        Text("No games found in ${folderName ?: "the folder"}.", textAlign = TextAlign.Center)
                        Spacer(Modifier.height(16.dp))
                        Button(onClick = onChangeFolder) { Text("Change folder") }
                    }
                }
            } else {
                LazyColumn(Modifier.fillMaxSize()) {
                    items(rows, key = { it.key }) { game -> GameRow(game, refresh, picture, onPlay, onHold) }
                }
            }
        }
    }
}

@Composable
private fun GameRow(game: GameEntry, refresh: Int, picture: (GameEntry) -> Bitmap?, onPlay: (GameEntry) -> Unit,
                    onHold: (GameEntry) -> Unit) {
    val bitmap = remember(game.id, refresh) { picture(game) }
    Row(
        Modifier.fillMaxWidth()
            .pointerInput(game) { detectTapGestures(onTap = { onPlay(game) }, onLongPress = { onHold(game) }) }
            .padding(horizontal = 16.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val frame = Modifier.width(96.dp).aspectRatio(1.5f).clip(RoundedCornerShape(4.dp))
        if (bitmap != null) {
            Image(bitmap.asImageBitmap(), contentDescription = null, modifier = frame,
                contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.None)
        } else {
            Box(frame.background(MaterialTheme.colorScheme.surfaceVariant))
        }
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(game.name, maxLines = 2, overflow = TextOverflow.Ellipsis)
            Text(playedText(game.lastPlayed) + if (game.outside) " · outside the folder" else "",
                fontSize = 12.sp, color = Color.Gray)
        }
    }
}

/** Holding a game in the list. [onRemove] only for games from outside the folder. */
@Composable
fun GameHoldMenu(game: GameEntry, onStartFresh: () -> Unit, onImport: () -> Unit, onExport: () -> Unit,
                 onRemove: (() -> Unit)?, onClose: () -> Unit) {
    AlertDialog(
        onDismissRequest = onClose,
        title = { Text(game.name, maxLines = 2, overflow = TextOverflow.Ellipsis) },
        text = {
            Column {
                val items = listOf("Start fresh (from the title screen)" to onStartFresh, "Import save…" to onImport,
                    "Export save…" to onExport) + listOfNotNull(onRemove?.let { "Remove from list" to it })
                for ((label, action) in items) {
                    TextButton(onClick = action, modifier = Modifier.fillMaxWidth()) {
                        Text(label, modifier = Modifier.fillMaxWidth())
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onClose) { Text("Cancel") } },
    )
}

/** Import over a save that exists: the old one is kept as .sav.bak. */
@Composable
fun ReplaceSaveDialog(name: String, onReplace: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Replace this game's save?") },
        text = { Text("$name already has a save. It will be kept as a backup, and the game will start from the title screen with the imported save.") },
        confirmButton = { TextButton(onClick = onReplace) { Text("Replace") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** Which keyboard key and gamepad button press each GBA button (from KeyMap, so it is always right). */
@Composable
fun ControlsDialog(onClose: () -> Unit) {
    AlertDialog(
        onDismissRequest = onClose,
        title = { Text("Controls") },
        text = {
            Column {
                Row { for (h in listOf("GBA", "Keyboard", "Gamepad")) Text(h, Modifier.weight(1f), fontWeight = FontWeight.Bold) }
                for (b in KeyMap.bindings) {
                    Row(Modifier.padding(top = 4.dp)) {
                        Text(b.button, Modifier.weight(1f))
                        Text(b.keyboard.values.joinToString(), Modifier.weight(1f))
                        Text(b.gamepad.values.joinToString(), Modifier.weight(1f))
                    }
                }
                Spacer(Modifier.height(12.dp))
                Text("On the phone: the on-screen buttons. They hide while a keyboard or gamepad is used; touch the screen to bring them back.")
            }
        },
        confirmButton = { TextButton(onClick = onClose) { Text("Close") } },
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
private fun MenuLayer(content: @Composable () -> Unit) {
    Box(
        Modifier.fillMaxSize().background(Color(0xB0000000)).pointerInput(Unit) { detectTapGestures { } },
        contentAlignment = Alignment.Center,
    ) {
        Surface(shape = RoundedCornerShape(16.dp), modifier = Modifier.padding(16.dp).widthIn(max = 440.dp)) {
            content()
        }
    }
}

/** Back while playing: the game is paused behind this menu. */
@Composable
fun GameMenu(title: String, fastSpeed: String, onFastSpeed: () -> Unit, onResume: () -> Unit, onSave: () -> Unit,
             onLoad: () -> Unit, onCheats: () -> Unit, onRestart: () -> Unit, onQuit: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(16.dp).width(260.dp)) {
            Text("Paused — $title", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(12.dp))
            for ((label, action) in listOf("▶  Resume" to onResume, "Fast-forward: $fastSpeed" to onFastSpeed,
                "Save state…" to onSave, "Load state…" to onLoad, "Cheats…" to onCheats,
                "Restart game" to onRestart, "Quit" to onQuit)) {
                FilledTonalButton(onClick = action, modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
                    Text(label)
                }
            }
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
        (1..States.SLOTS).map { if (states.used(it)) format.format(Date(states.savedAt(it))) else "empty" }
    }
    MenuLayer {
        Column(Modifier.padding(12.dp).verticalScroll(rememberScrollState())) {
            Text(if (saving) "Save state" else "Load state", style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.padding(4.dp))
            for (row in 0 until States.SLOTS / 2) {
                Row {
                    for (col in 0..1) {
                        val slot = row * 2 + col + 1
                        val enabled = saving || used[slot - 1]
                        Column(
                            Modifier.weight(1f).padding(4.dp).clip(RoundedCornerShape(8.dp))
                                .background(MaterialTheme.colorScheme.surfaceVariant)
                                .clickable(enabled = enabled) { onPick(slot) }.padding(6.dp),
                        ) {
                            val picture = pictures[slot - 1]
                            val frame = Modifier.fillMaxWidth().aspectRatio(1.5f).clip(RoundedCornerShape(4.dp))
                            if (picture != null) {
                                Image(picture.asImageBitmap(), contentDescription = "slot $slot", modifier = frame,
                                    contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.None)
                            } else {
                                Box(frame.border(1.dp, Color.Gray, RoundedCornerShape(4.dp)))
                            }
                            Text("$slot · ${dates[slot - 1]}", fontSize = 12.sp,
                                color = if (enabled) Color.Unspecified else Color.Gray,
                                modifier = Modifier.padding(top = 4.dp))
                        }
                    }
                }
            }
            TextButton(onClick = onBack, modifier = Modifier.align(Alignment.End)) { Text("Back") }
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
        Column(Modifier.padding(16.dp).width(300.dp)) {
            Text("Cheats — $title", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            Column(Modifier.heightIn(max = 360.dp).verticalScroll(rememberScrollState())) {
                if (cheats.isEmpty()) Text("No cheats yet.", Modifier.padding(vertical = 8.dp))
                cheats.forEachIndexed { i, c ->
                    Row(
                        Modifier.fillMaxWidth().padding(vertical = 3.dp).clip(RoundedCornerShape(8.dp))
                            .background(MaterialTheme.colorScheme.surfaceVariant),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(c.name, Modifier.weight(1f).clickable { onEdit(i) }.padding(12.dp))
                        Switch(checked = c.on, onCheckedChange = { onToggle(i, it) }, modifier = Modifier.padding(end = 8.dp))
                    }
                }
            }
            Spacer(Modifier.height(8.dp))
            Button(onClick = onAdd, modifier = Modifier.fillMaxWidth()) { Text("+ Add cheat") }
            TextButton(onClick = onBack, modifier = Modifier.align(Alignment.End)) { Text("Back") }
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
                    OutlinedButton(onClick = { picking = true }) { Text("Type: ${CHEAT_TYPES[type]} ▾") }
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
                    Text(hint, color = if (found != null && found[1] == 0 && error == null) Color.Unspecified
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
                if (onDelete != null) TextButton(onClick = onDelete) { Text("Delete") }
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
        confirmButton = { TextButton(onClick = onDelete) { Text("Delete") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun ReplaceDialog(slot: Int, onReplace: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Replace slot $slot?") },
        text = { Text("The state saved there is overwritten.") },
        confirmButton = { TextButton(onClick = onReplace) { Text("Replace") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun RestartDialog(onRestart: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Restart from the title screen?") },
        text = { Text("The current moment is lost unless it's saved in a slot.") },
        confirmButton = { TextButton(onClick = onRestart) { Text("Restart") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/**
 * The game (a SurfaceView handed to the native render thread) and the touch buttons, in layout A; for a solar
 * cartridge also the ☀ button with the light slider ([manualLight] null = Auto, the phone's light sensor).
 */
@Composable
fun GameScreen(controlsHidden: Boolean, fastOn: Boolean, onTouchMask: (Int) -> Unit, onShowControls: () -> Unit,
               onDrawFailed: () -> Unit, solar: Boolean = false, manualLight: Int? = null,
               onManualLight: (Int?) -> Unit = {}) {
    BoxWithConstraints(Modifier.fillMaxSize().background(Color.Black)) {
        val sideways = maxWidth > maxHeight
        val game: @Composable (Modifier) -> Unit = { modifier ->
            AndroidView(modifier = modifier, factory = { gameSurface(it, onDrawFailed) })
        }
        val buttons: @Composable (Modifier) -> Unit = { modifier ->
            AndroidView(
                modifier = modifier,
                factory = { TouchControlsView(it).also { onTouchMask(0) } }, // a new view (e.g. after turning): nothing held
                update = { v ->
                    v.sideways = sideways
                    v.hidden = controlsHidden
                    v.fastOn = fastOn
                    v.onMask = onTouchMask
                    v.onShow = onShowControls
                },
            )
        }
        if (sideways) {
            Box(Modifier.fillMaxSize()) {
                game(Modifier.fillMaxSize()) // the game centred, black bars left and right
                buttons(Modifier.fillMaxSize()) // see-through, over the bars
            }
        } else {
            Column(Modifier.fillMaxSize()) {
                game(Modifier.fillMaxWidth().aspectRatio(1.5f)) // 240×160 across the full width, at the top
                buttons(Modifier.fillMaxWidth().weight(1f))
            }
        }
        if (solar) SunControl(manualLight, onManualLight, Modifier.align(Alignment.BottomStart))
    }
}

/** ☀: opens a light slider (moving it sets the level by hand) with an Auto switch (the phone's light sensor). */
@Composable
private fun SunControl(level: Int?, onLevel: (Int?) -> Unit, modifier: Modifier) {
    var open by remember { mutableStateOf(false) }
    Column(modifier.padding(8.dp)) {
        if (open) {
            Surface(shape = RoundedCornerShape(12.dp), color = Color(0xE0202020)) {
                Column(Modifier.padding(12.dp).width(260.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Light: ${level ?: "sensor"}", color = Color.White, modifier = Modifier.weight(1f))
                        Text("Auto ", color = Color.White)
                        Switch(checked = level == null, onCheckedChange = { onLevel(if (it) null else 128) })
                    }
                    Slider(value = (level ?: 128).toFloat(), onValueChange = { onLevel(it.roundToInt()) },
                        valueRange = 0f..255f)
                }
            }
            Spacer(Modifier.height(4.dp))
        }
        TextButton(onClick = { open = !open }) { Text("☀", fontSize = 28.sp, color = Color(0xFFFFD54F)) }
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

Replace `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Intent
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
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
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
    private var showControls by mutableStateOf(false)
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
        fastSpeed = getSharedPreferences("settings", MODE_PRIVATE).getInt("fast_speed", 4)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
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
                        onOpenGame = { openRom.launch(arrayOf("*/*")) }, onControls = { showControls = true })
                } else {
                    LibraryScreen(rows, folderName, scanning, message, libraryRefresh,
                        picture = { g -> if (g.id.isEmpty()) null else States(filesDir, g.id).loadPicture(States.AUTO) },
                        onPlay = { open(it) }, onHold = { holdGame = it },
                        onOpenGame = { openRom.launch(arrayOf("*/*")) }, onChangeFolder = { pickFolder.launch(null) },
                        onControls = { showControls = true })
                }
                holdGame?.let { g ->
                    GameHoldMenu(g,
                        onStartFresh = { holdGame = null; open(g, resumeAuto = false) },
                        onImport = { holdGame = null; saveGame = g; importFor.launch(arrayOf("*/*")) },
                        onExport = { holdGame = null; exportSave(g) },
                        onRemove = if (g.outside) ({ holdGame = null; removeGame(g) }) else null,
                        onClose = { holdGame = null })
                }
                replaceSave?.let { (g, bytes) ->
                    ReplaceSaveDialog(g.name, onReplace = { replaceSave = null; importSave(g, bytes) },
                        onCancel = { replaceSave = null })
                }
                if (showControls) ControlsDialog { showControls = false }
                if (askImport) {
                    ImportDialog(importError,
                        onStartFresh = { askImport = false; startGame() },
                        onImport = { pickSave.launch(arrayOf("*/*")) },
                        onCancel = { askImport = false; NativeBridge.stop() })
                }
                val st = states
                when (menu) {
                    MenuPage.MAIN -> GameMenu(currentTitle, speedLabel(fastSpeed), onFastSpeed = ::nextFastSpeed,
                        onResume = ::resumeGame, onSave = { menu = MenuPage.SAVE },
                        onLoad = { menu = MenuPage.LOAD }, onCheats = { menu = MenuPage.CHEATS },
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
                BackHandler(enabled = playing && menu == null) { openMenu() }
                BackHandler(enabled = playing && menu != null) {
                    if (menu == MenuPage.MAIN) resumeGame() else menu = MenuPage.MAIN
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

    /** "Open a game…": the game's row if the list has it (e.g. it is in the folder), else a new outside one. */
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
        if (NativeBridge.loadRom(rom, File(saves, "$gameId.sav").path) != 0) {
            message = "Not a GBA game."
            return
        }
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

    private fun speedLabel(speed: Int) = if (speed == NativeBridge.FF_MAX) "Max" else "$speed×"

    /** The menu's fast-forward row: 2× → 4× → 8× → Max → 2×, remembered for all games. */
    private fun nextFastSpeed() {
        fastSpeed = when (fastSpeed) {
            2 -> 4
            4 -> 8
            8 -> NativeBridge.FF_MAX
            else -> 2
        }
        getSharedPreferences("settings", MODE_PRIVATE).edit().putInt("fast_speed", fastSpeed).apply()
        sendKeys() // a switched-on fast mode uses the new speed
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

```bash
git rm app/src/main/java/com/tengen/gbaemu/LastGame.kt
```

- [x] **Step 2: Build and run every test**

```bash
bash tools/dev.sh jvmtest && bash tools/dev.sh unit && bash tools/dev.sh test
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
```

Expected: Kotlin 54/54, `399 tests, 0 failed` (the core unchanged), CTest 120/120, on-device `tests="47" failures="0"`.

- [x] **Step 3: Claude's check on the emulator (adb)** — with a test folder `Download/GBA` holding Claude's own test files: `m7_demo.gba`, `obj_aff.zip` (obj_aff.gba and a readme) and `notes.zip` (no game). The user's list is kept aside first and put back at the end (the folder's permission stays granted: a new folder only gives the *listed* old folder's permission back, and the list is empty while the check runs). (Found while prototyping: Android refuses the Download folder itself as a games folder — hence a folder inside it.)

Use a scratchpad folder `$SP`. Helpers:

```bash
ADB_EXE="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk/platform-tools/adb"
EMU=$(MSYS_NO_PATHCONV=1 "$ADB_EXE" devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }')
ADB() {  # the emulator only: refuse when none is running (never fall back to a USB phone)
    [ -n "$EMU" ] || { echo "ADB: no emulator running - refusing" >&2; return 1; }
    MSYS_NO_PATHCONV=1 "$ADB_EXE" -s "$EMU" "$@"
}
shot() { ADB exec-out screencap -p > "$SP/$1.png"; }
texts() { ADB shell uiautomator dump /sdcard/ui.xml >/dev/null; ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -oE 'text="[^"]+"'; }
centre_of() {  # "x y" of the first element whose $1 (text or content-desc) is exactly $2; waits up to 10 s
    for i in 1 2 3 4 5 6 7 8 9 10; do
        ADB shell uiautomator dump /sdcard/ui.xml >/dev/null
        b=$(ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -F "$1=\"$2\"" | grep -oE 'bounds="\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]"' | head -1)
        if [ -n "$b" ]; then
            set -- $(echo "$b" | grep -oE '[0-9]+')
            echo "$((($1 + $3) / 2)) $((($2 + $4) / 2))"
            return 0
        fi
        sleep 1
    done
    echo "not on screen: $1=$2" >&2
    return 1
}
tap_on() { local p; p=$(centre_of "$1" "$2") || return 1; ADB shell input tap $p; }
hold_on() {  # press and hold 1 s (found while prototyping: "input swipe x y x y 900" is no long press for Compose)
    local p; p=$(centre_of "$1" "$2") || return 1
    set -- $p
    ADB shell "input motionevent DOWN $1 $2; sleep 1; input motionevent UP $1 $2"
}
bottom_of() {  # the bottom of a screenshot (where a toast shows), to look at
    python -c "from PIL import Image; import sys; Image.open(sys.argv[1]).crop((0, 1900, 1080, 2300)).save(sys.argv[2])" "$(cygpath -w "$SP/$1.png")" "$(cygpath -w "$SP/$1_bottom.png")"
}
```

Then (as a script, `bash <file>`; it stops if the first-start screen does not show):

```bash
mkdir -p "$SP/gbadir" && cp tests/roms/tonc/m7_demo.gba "$SP/gbadir/"
python - "$(cygpath -w "$SP/gbadir")" <<'PYEOF'
import sys, zipfile
d = sys.argv[1]
with zipfile.ZipFile(d + '/obj_aff.zip', 'w') as z:
    z.writestr('readme.txt', 'test')
    z.write('tests/roms/tonc/obj_aff.gba', 'obj_aff.gba')
with zipfile.ZipFile(d + '/notes.zip', 'w') as z:
    z.writestr('notes.txt', 'no game here')
PYEOF
ADB shell mkdir -p /sdcard/Download/GBA
for f in m7_demo.gba obj_aff.zip notes.zip; do ADB push "$(cygpath -w "$SP/gbadir/$f")" /sdcard/Download/GBA/$f; done
ADB shell settings put system accelerometer_rotation 0; ADB shell settings put system user_rotation 0
ADB shell input keyevent KEYCODE_HOME; ADB shell am force-stop com.tengen.gbaemu; ADB shell am force-stop com.google.android.documentsui
bash tools/dev.sh install
ADB shell run-as com.tengen.gbaemu mv files/library.json files/library.json.keep   # the user's list, put back below
bash tools/dev.sh launch; sleep 3; texts | head -5
tap_on text "Choose games folder" || { echo "STOP: no first-start screen"; exit 1; }
sleep 3; tap_on text "Download"; sleep 2; tap_on text "GBA"; sleep 2; tap_on text "USE THIS FOLDER"; sleep 2
tap_on text "ALLOW"; sleep 4
texts; shot list                                                            # the games A-Z, all 'Never played'
tap_on text "m7_demo"; sleep 4; shot playing
ADB shell input keyevent KEYCODE_BACK; sleep 1.5; texts | head -1
tap_on text "Quit"; sleep 3; texts | head -4; shot quit                     # m7_demo first, 'Today …', its picture
hold_on text "obj_aff"; sleep 1.5; texts                                    # the hold menu, no 'Remove from list'
tap_on text "Export save…"; sleep 0.8; shot export; bottom_of export        # look: 'No save for this game yet.'
ADB logcat -d -s AndroidRuntime:E | tail -3
ADB shell am force-stop com.tengen.gbaemu
ADB shell run-as com.tengen.gbaemu mv files/library.json.keep files/library.json   # the user's list back
bash tools/dev.sh launch; sleep 4; texts | head -6                          # the user's list as before
```

Expected:
- The first-start screen: 'GBA Emulator', the help with the Download line, 'Choose games folder', 'Open a single game…', 'Controls'.
- `list`: 'Games', '⋮', then every `.gba` / game zip in `Download/GBA` A-Z with 'Never played' — at least 'm7_demo' and 'obj_aff'; **no 'notes'**.
- In the game: 'Paused — m7_demo'. After Quit: 'm7_demo' first with 'Today …' and its picture (`quit.png`).
- Holding obj_aff: 'obj_aff', 'Start fresh (from the title screen)', 'Import save…', 'Export save…', 'Cancel' (no 'Remove from list'); `export_bottom.png` shows 'No save for this game yet.'
- No `AndroidRuntime` crash; at the end the user's own list is shown again.

- [x] **Step 4: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/FolderScan.kt app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-6a-library.md
git commit -m "feat(android): the game library - games folder, list with pictures, hold menu; replaces the start screen" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 5: User check and milestone 6a exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test && bash tools/dev.sh unit && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest
```

Expected: CTest 120/120 (sanitizers on, no reports), 399 unit tests, Kotlin 54, on-device 47 (emulator only).

- [x] **Step 2: The user's check** (the app installed on the emulator)

Their games folder (a folder inside Download) is listed; game A opened from the list continues where it was left; after Quit it is first with its picture and time; hold → Export save… → the `.sav` saved to Download.

- [x] **Step 3: Docs**

`docs/PROGRESS.md` (6a ✅ with the numbers and the user's check; Next = milestone 6b brainstorming — settings + controller remapping); spec status → `Implemented — milestone 6a complete <date> (tag milestone-6a)`; parent spec §10 row 6 → 6a ✅; `CLAUDE.md` key files → 6b needs brainstorming; tick every box here.

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`, record the merge in PROGRESS.md; keep the branch `milestone-6` for 6b and 6c if the user wants (else delete it and branch again from `main`); delete the prototype and replay worktrees (`rm -rf` the folders in Git Bash, `git worktree prune`, `git branch -D proto-6a replay-6a`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 6a complete - game library" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-6a
```
