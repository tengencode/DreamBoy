# Milestone 11 — Backup and Restore — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** Settings → Backup makes one `.zip` with the app's saves, save states, settings and controls, cheats, imported skins and chosen box art, by hand or automatically into a picked folder; Restore replaces everything after a safety copy, with Undo; Android's own backup is off.

**Architecture:** Pure Kotlin writes, checks and unpacks the zip (`Backup`, `java.util.zip`), swaps the phone's data with a safety copy and roll-back (`Restore`), and decides automatic backups (`BackupSchedule`) — all tested on the PC with temporary folders and `MemoryStore`s. Android code adds the folder access (`BackupFolder`, Android's document tree), the Backup page, the restore question and the spinner, the wiring in `MainActivity`, and the manifest's backup rules. The emulator core and the native code are not touched.

**Tech Stack:** Kotlin, Jetpack Compose (Material 3 1.4.0, material-icons-extended 1.7.8), `java.util.zip`, `DocumentsContract`, `SharedPreferences`, JUnit 4, Compose UI test 1.11.4, Gradle `--offline`, Git Bash `tools/dev.sh`.

**Spec:** `docs/superpowers/specs/2026-10-04-milestone-11-backup-design.md`

---

## Conventions

- Git Bash from the repo root, on the branch `milestone-11`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push>` (the Pixel 9 phone emulator `Pixel_9_API_36_1`; never the user's USB phones). Gradle runs `--offline`; nothing is downloaded. Ask before restarting or cold-booting the emulator.
- The core and the native code are not touched: CTest stays 120/120 (run once in Task 7).
- Counting tests: Kotlin unit tests `ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'` (total, failures); on-device `grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml` after `rm -rf app/build/outputs/androidTest-results`.
- Kotlin: 152 → 155 (Task 1) → 163 (Task 2) → 169 (Task 3) → 173 (Task 4) → 175 (Task 5) → 176 (Task 7: a fix found in Claude's check). On-device: 70 → 73 (Task 5) → 74 (Task 6).
- Test-first: each task's tests are written first and fail (they don't compile without the new code), then the code.
- One commit per task; messages end with the Co-Authored-By trailer. Each commit also includes `docs/PROGRESS.md` (the task's Completed entry, the 11 task table, Project status, Next, Last updated) and this plan (ticked boxes). The tasks run one after another without stopping; **no user check** (spec §1).
- Every string in `src/main` must pass `UiTextTest` (no "…", "...", "&" in a string literal).

## File map

```
app/src/main/java/com/tengen/gbaemu/Store.kt (all, clear), BoxArt.kt (choices, replaceChoices);
  app/src/test/.../StoreTest.kt (new), BoxArtTest.kt                                                           Task 1
app/src/main/java/com/tengen/gbaemu/Backup.kt (new); app/src/test/.../BackupTest.kt (new)                       Task 2
app/src/main/java/com/tengen/gbaemu/Restore.kt (new); app/src/test/.../RestoreTest.kt (new)                     Task 3
app/src/main/java/com/tengen/gbaemu/BackupSchedule.kt (new); app/src/test/.../BackupScheduleTest.kt (new)       Task 4
app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt, Screens.kt;
  app/src/test/.../BackupTextTest.kt (new); app/src/androidTest/.../ScreensTest.kt                             Task 5
app/src/main/java/com/tengen/gbaemu/BackupFolder.kt (new), MainActivity.kt, AndroidManifest.xml,
  app/src/main/res/xml/data_extraction_rules.xml (new); app/src/androidTest/.../BackupDeviceTest.kt (new)       Task 6
(Claude's check on the emulator, docs, tag)                                                                     Task 7
```

(`app/src/test/...` = `app/src/test/java/com/tengen/gbaemu/`; `app/src/androidTest/...` = `app/src/androidTest/java/com/tengen/gbaemu/`.)

---

### Task 1: Every setting at once (`IntStore.all` / `clear`) and the box-art choices

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Store.kt`, `app/src/main/java/com/tengen/gbaemu/BoxArt.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/StoreTest.kt` (new, 1), `app/src/test/java/com/tengen/gbaemu/BoxArtTest.kt` (+2)

- [x] **Step 1: Write the failing tests**

Create `StoreTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class StoreTest {
    @Test
    fun aMemoryStoreGivesEveryValueAndEmpties() {
        val s = MemoryStore()
        s.put("a", 1)
        s.put("b", 2)
        s.put("a", null)
        s.put("c", 3)
        assertEquals(mapOf("b" to 2, "c" to 3), s.all())
        s.clear()
        assertEquals(emptyMap<String, Int>(), s.all())
        assertEquals(null, s.get("b"))
    }
}
```

At the end of `BoxArtTest`:

```kotlin
    @Test
    fun theChoicesAreTheUsersOwnAndRemovedOnly() {
        val art = BoxArt(dir)
        art.found("aaaaaaaa", "A", jpeg, 1)
        art.none("bbbbbbbb", "", 1)
        art.own("cccccccc", jpeg)
        art.remove("dddddddd")
        assertEquals(mapOf("cccccccc" to BoxArt.State.OWN, "dddddddd" to BoxArt.State.REMOVED), art.choices())
    }

    @Test
    fun replacingTheChoicesKeepsTheDownloadedOnes() {
        val art = BoxArt(dir)
        art.found("aaaaaaaa", "A", jpeg, 1)
        art.own("cccccccc", jpeg)
        art.remove("dddddddd")
        val mine = byteArrayOf(7, 7)
        art.replaceChoices(mapOf("eeeeeeee" to BoxArt.State.OWN, "aaaaaaaa" to BoxArt.State.REMOVED, "ffffffff" to BoxArt.State.OWN),
            mapOf("eeeeeeee" to mine)) // ffffffff: no picture, left out
        val again = BoxArt(dir)
        assertEquals(mapOf("eeeeeeee" to BoxArt.State.OWN, "aaaaaaaa" to BoxArt.State.REMOVED), again.choices())
        assertArrayEquals(mine, again.picture("eeeeeeee")!!.readBytes())
        assertNull(again.picture("aaaaaaaa"))
        assertNull(again.entry("cccccccc")) // the old own choice is gone (looked up again)
        assertFalse(File(dir, "cccccccc.jpg").exists())
        assertNull(again.entry("dddddddd"))
        assertEquals("A", again.entry("aaaaaaaa")!!.name) // what was known is kept
    }
```

- [x] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile errors (`all`, `clear`, `choices`, `replaceChoices` unknown).

- [x] **Step 3: Write the code**

`Store.kt` — in `interface IntStore` after `put`:

```kotlin
    /** Every value by name (11: backups). */
    fun all(): Map<String, Int>

    /** Removes every value (11: a restore). */
    fun clear()
```

In `MemoryStore`:

```kotlin
    override fun all(): Map<String, Int> = HashMap(map)

    override fun clear() = map.clear()
```

In `PrefsStore`:

```kotlin
    override fun all(): Map<String, Int> = prefs.all.mapNotNull { (k, v) -> (v as? Int)?.let { k to it } }.toMap()

    override fun clear() {
        prefs.edit().clear().commit()
    }
```

`BoxArt.kt` — after `remove`:

```kotlin
    /** The user's own choices (own pictures and removed boxes), for a backup (11). */
    @Synchronized
    fun choices(): Map<String, State> =
        entries.filterValues { it.state == State.OWN || it.state == State.REMOVED }.mapValues { it.value.state }

    /**
     * A restore (11): the user's own choices become [choices] (an own one needs its picture in [pictures], else it is
     * left out); the old own pictures are deleted; found and not-found entries stay.
     */
    @Synchronized
    fun replaceChoices(choices: Map<String, State>, pictures: Map<String, ByteArray>) {
        for ((id, e) in entries.toList()) if (e.state == State.OWN || e.state == State.REMOVED) {
            if (e.state == State.OWN) pictureFile(id).delete()
            entries.remove(id)
        }
        for ((id, state) in choices) {
            val old = entries[id]
            when (state) {
                State.OWN -> {
                    val jpeg = pictures[id] ?: continue
                    if (writePicture(id, jpeg)) entries[id] = Entry(State.OWN, old?.name ?: "", old?.checked ?: 0)
                }
                State.REMOVED -> {
                    pictureFile(id).delete()
                    entries[id] = Entry(State.REMOVED, old?.name ?: "", old?.checked ?: 0)
                }
                else -> {}
            }
        }
        write()
    }
```

and split `put` so the file can be written without a change:

```kotlin
    private fun put(id: String, e: Entry) {
        entries[id] = e
        write()
    }

    private fun write() {
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
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 155, 0 failures.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Store.kt app/src/main/java/com/tengen/gbaemu/BoxArt.kt app/src/test/java/com/tengen/gbaemu/StoreTest.kt app/src/test/java/com/tengen/gbaemu/BoxArtTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-11-backup.md
git commit -m "feat: every setting at once and the box-art choices, for backups

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The backup file (`Backup`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/Backup.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/BackupTest.kt` (new, 8 tests)

- [x] **Step 1: Write the failing test**

```kotlin
package com.tengen.gbaemu

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class BackupTest {
    private val id = "e86b3224"
    private val root = Files.createTempDirectory("backup").toFile()
    private val phone = File(root, "phone")

    private fun put(path: String, text: String) = File(phone, path).apply { parentFile?.mkdirs(); writeText(text) }

    /** A phone with one of everything, and things that must stay out. */
    private fun fill() {
        put("saves/$id.sav", "save")
        put("saves/$id.sav.bak", "old save")
        put("states/$id/auto.state", "auto")
        put("states/$id/auto.png", "auto picture")
        put("states/$id/slot10.state", "ten")
        put("states/$id/auto.state.old", "set aside")
        put("cheats/$id.json", "[]")
        put("layout.json", "{}")
        put("skins/1/info.json", "{}")
        put("skins/1/pictures/a b.png", "picture")
        put("skins/.incoming/x", "half")
        put("boxart/cccccccc.jpg", "mine")
        put("boxart/aaaaaaaa.jpg", "downloaded")
        put("boxart/boxart.json", "{}")
        put("bios/gba_bios.bin", "nintendo")
        put("library.json", "{}")
    }

    private val source get() = Backup.Source(phone, mapOf("settings" to mapOf("smooth" to 1), "keys" to emptyMap()),
        mapOf("cccccccc" to BoxArt.State.OWN, "dddddddd" to BoxArt.State.REMOVED))

    private fun backup(): ByteArray = ByteArrayOutputStream().also { Backup.write(source, it, "0.1", "Pixel 9", 1_759_600_000_000L) }.toByteArray()

    private fun names(zip: ByteArray): List<String> = ZipInputStream(ByteArrayInputStream(zip)).use { z ->
        generateSequence { z.nextEntry }.map { it.name }.toList()
    }

    private fun zip(vararg entries: Pair<String, String>): ByteArray = ByteArrayOutputStream().also { out ->
        ZipOutputStream(out).use { z -> for ((n, t) in entries) { z.putNextEntry(ZipEntry(n)); z.write(t.toByteArray()); z.closeEntry() } }
    }.toByteArray()

    private val goodInfo = Backup.Info(1, "0.1", 5, "Pixel 9", 1, 1, 0, 0).toJson()

    private fun refused(bytes: ByteArray, max: Long = Backup.MAX_BYTES) {
        val into = File(root, "into")
        try {
            Backup.unpack(ByteArrayInputStream(bytes), into, max)
            fail("expected NotABackup")
        } catch (e: Backup.NotABackup) {
            assertFalse(into.exists())
        }
    }

    @Test
    fun aBackupHoldsExactlyTheAppsParts() {
        fill()
        val zip = backup()
        assertEquals(listOf("backup.json", "prefs/settings.json", "prefs/keys.json", "prefs/game_settings.json", "boxart/choices.json",
            "boxart/cccccccc.jpg", "cheats/$id.json", "layout.json", "saves/$id.sav", "skins/1/info.json", "skins/1/pictures/a b.png",
            "states/$id/auto.png", "states/$id/auto.state", "states/$id/slot10.state"), names(zip))
        val info = Backup.unpack(ByteArrayInputStream(zip), File(root, "out"))
        assertEquals(Backup.Info(1, "0.1", 1_759_600_000_000L, "Pixel 9", games = 1, states = 2, cheats = 1, skins = 1), info)
    }

    @Test
    fun theInfoIsWrittenAndRead() {
        val info = Backup.Info(1, "0.1", 9, "One \"Plus\"", 2, 3, 4, 5)
        assertEquals(info, Backup.Info.parse(info.toJson()))
        assertNull(Backup.Info.parse("{\"version\": 1}"))
        assertNull(Backup.Info.parse("nonsense"))
    }

    @Test
    fun onlyTheAppsPathsAreAllowed() {
        for (p in listOf("backup.json", "saves/$id.sav", "states/$id/quick.png", "states/$id/slot1.state", "cheats/$id.json", "layout.json",
            "skins/12/info.json", "skins/1/a/b c.pdf", "boxart/choices.json", "boxart/$id.jpg", "prefs/keys.json")) {
            assertTrue(p, Backup.allowed(p))
        }
        for (p in listOf("../x", "/saves/$id.sav", "saves/../saves/$id.sav", "skins/1/../../x", "skins/a/x", "skins/1/", "skins/1/./x",
            "bios/gba_bios.bin", "library.json", "states/$id/auto.state.old", "states/$id/slot11.state", "saves\\$id.sav",
            "prefs/other.json", "boxart/boxart.json")) {
            assertFalse(p, Backup.allowed(p))
        }
    }

    @Test
    fun unpackingGivesTheFilesBack() {
        fill()
        val out = File(root, "out")
        Backup.unpack(ByteArrayInputStream(backup()), out)
        assertEquals("ten", File(out, "states/$id/slot10.state").readText())
        assertEquals("picture", File(out, "skins/1/pictures/a b.png").readText())
        assertEquals(mapOf("smooth" to 1), Backup.readInts(File(out, "prefs/settings.json").readText()))
        assertEquals(emptyMap<String, Int>(), Backup.readInts(File(out, "prefs/game_settings.json").readText()))
        assertEquals(mapOf("cccccccc" to BoxArt.State.OWN, "dddddddd" to BoxArt.State.REMOVED),
            Backup.readChoices(File(out, "boxart/choices.json").readText()))
    }

    @Test
    fun unsafeOrForeignFilesAreRefusedAndLeaveNothing() {
        refused(zip("backup.json" to goodInfo, "../evil" to "x"))
        assertFalse(File(root, "evil").exists())
        refused(zip("saves/$id.sav" to "x")) // no backup.json
        refused(zip("backup.json" to goodInfo.replace("\"version\": 1", "\"version\": 2")))
        refused(zip("backup.json" to goodInfo, "prefs/keys.json" to "{\"a\": \"x\"}"))
        refused(zip("backup.json" to goodInfo, "boxart/choices.json" to "{\"$id\": \"found\"}"))
        refused("just text, not a zip".toByteArray())
    }

    @Test
    fun aBackupThatUnpacksTooBigIsRefused() {
        refused(zip("backup.json" to goodInfo, "saves/$id.sav" to "x".repeat(1000)), max = 999)
    }

    @Test
    fun settingsAndChoicesAreReadStrictly() {
        assertEquals(mapOf("a" to 1, "b" to -2), Backup.readInts("{\"a\": 1, \"b\": -2}"))
        for (bad in listOf("{\"a\": 1.5}", "{\"a\": true}", "[1]")) {
            try {
                Backup.readInts(bad)
                fail(bad)
            } catch (e: Exception) {
            }
        }
        try {
            Backup.readChoices("{\"../x\": \"own\"}")
            fail("a bad id")
        } catch (e: Exception) {
        }
    }

    @Test
    fun sizesReadNicelyAndAreCounted() {
        assertEquals("1 KB", Backup.sizeText(500))
        assertEquals("2 KB", Backup.sizeText(1536))
        assertEquals("12.0 MB", Backup.sizeText(12L shl 20))
        val c = Backup.Counting(ByteArrayOutputStream())
        c.write(1)
        c.write(ByteArray(10), 2, 5)
        assertEquals(6L, c.count)
    }
}
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile error (`Backup` unknown).

- [x] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/Backup.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.io.FilterOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/**
 * The app's backup file (11, spec §3): a zip of the saves, states, cheats, touch layouts, imported skins, the user's
 * own box art and the three preference files, with `backup.json` saying what it is. [write] makes one; [unpack]
 * checks one and unpacks it. Not the BIOS file, the game list, ROMs or downloaded box art.
 */
object Backup {
    const val VERSION = 1
    const val INFO = "backup.json"
    const val MAX_BYTES = 1L shl 30

    /** The preference files a backup holds, by their names in Android. */
    val PREFS = listOf("settings", "keys", "game_settings")

    /** What a backup says about itself. */
    data class Info(val version: Int, val app: String, val made: Long, val phone: String, val games: Int, val states: Int,
                    val cheats: Int, val skins: Int) {
        fun toJson(): String = "{\"version\": $version, \"app\": ${Json.str(app)}, \"made\": $made, \"phone\": ${Json.str(phone)}, " +
            "\"games\": $games, \"states\": $states, \"cheats\": $cheats, \"skins\": $skins}"

        companion object {
            fun parse(text: String): Info? = try {
                val r = Json.Reader(text)
                val m = r.value() as Map<*, *>
                r.end()
                fun n(key: String) = (m[key] as Double).toLong()
                Info(n("version").toInt(), m["app"] as String, n("made"), m["phone"] as String, n("games").toInt(),
                    n("states").toInt(), n("cheats").toInt(), n("skins").toInt())
            } catch (e: Exception) {
                null
            }
        }
    }

    /** What goes into a backup: the app's files folder, the preference files' values, the user's box-art choices. */
    class Source(val filesDir: File, val prefs: Map<String, Map<String, Int>>, val boxChoices: Map<String, BoxArt.State>)

    /** Not a backup this app can restore. */
    class NotABackup : Exception()

    /** Counts the bytes written through it (the size in the "Backup saved" note). */
    class Counting(out: OutputStream) : FilterOutputStream(out) {
        var count = 0L
            private set

        override fun write(b: Int) {
            out.write(b)
            count++
        }

        override fun write(b: ByteArray, off: Int, len: Int) {
            out.write(b, off, len)
            count += len
        }
    }

    private const val ID = "[0-9a-f]{8}"
    private val PATHS = listOf(
        Regex("backup\\.json"),
        Regex("saves/$ID\\.sav"),
        Regex("states/$ID/(auto|quick|slot([1-9]|10))\\.(state|png)"),
        Regex("cheats/$ID\\.json"),
        Regex("layout\\.json"),
        Regex("skins/[0-9]+/.+"),
        Regex("boxart/choices\\.json"),
        Regex("boxart/$ID\\.jpg"),
        Regex("prefs/(settings|keys|game_settings)\\.json"),
    )

    /** A path a backup may hold: one of the app's own, with no empty, `.` or `..` part, no leading `/` and no `\`. */
    fun allowed(path: String): Boolean = '\\' !in path &&
        path.split('/').none { it.isEmpty() || it == "." || it == ".." } && PATHS.any { it.matches(path) }

    /** Writes a backup of [src] into [out] (finished, not closed); what it holds. */
    fun write(src: Source, out: OutputStream, app: String, phone: String, now: Long): Info {
        val files = files(src.filesDir, src.boxChoices)
        val games = files.keys.filter { it.startsWith("saves/") || it.startsWith("states/") }.map { it.substringAfter('/').take(8) }.toSet()
        val info = Info(VERSION, app, now, phone, games.size, files.keys.count { it.endsWith(".state") },
            files.keys.count { it.startsWith("cheats/") }, files.keys.filter { it.startsWith("skins/") }.map { it.split('/')[1] }.toSet().size)
        val zip = ZipOutputStream(out)
        fun put(path: String, bytes: ByteArray) {
            zip.putNextEntry(ZipEntry(path))
            zip.write(bytes)
            zip.closeEntry()
        }
        put(INFO, info.toJson().toByteArray())
        for (name in PREFS) put("prefs/$name.json", intsJson(src.prefs[name].orEmpty()).toByteArray())
        put("boxart/choices.json", choicesJson(src.boxChoices).toByteArray())
        for ((path, f) in files) {
            zip.putNextEntry(ZipEntry(path))
            f.inputStream().use { it.copyTo(zip) }
            zip.closeEntry()
        }
        zip.finish()
        return info
    }

    /** The backup's files by path (all but backup.json, the preference files and choices.json), sorted. */
    private fun files(filesDir: File, boxChoices: Map<String, BoxArt.State>): Map<String, File> {
        val out = sortedMapOf<String, File>()
        fun add(path: String, f: File) {
            if (f.isFile && allowed(path)) out[path] = f
        }
        File(filesDir, "saves").listFiles()?.forEach { add("saves/${it.name}", it) }
        File(filesDir, "states").listFiles()?.filter { it.isDirectory }?.forEach { d ->
            d.listFiles()?.forEach { add("states/${d.name}/${it.name}", it) }
        }
        File(filesDir, "cheats").listFiles()?.forEach { add("cheats/${it.name}", it) }
        add("layout.json", File(filesDir, "layout.json"))
        val skins = File(filesDir, "skins")
        skins.listFiles()?.filter { it.isDirectory && it.name.toIntOrNull() != null }?.forEach { d ->
            d.walkTopDown().filter { it.isFile }.forEach { add("skins/" + it.relativeTo(skins).invariantSeparatorsPath, it) }
        }
        for ((id, state) in boxChoices) if (state == BoxArt.State.OWN) add("boxart/$id.jpg", File(filesDir, "boxart/$id.jpg"))
        return out
    }

    /**
     * Checks [input] and unpacks it into [into] (emptied first): only the app's paths, at most [maxBytes] unpacked, a
     * readable `backup.json` of this version or older, readable preference and choice files. Otherwise [NotABackup]
     * and [into] is deleted.
     */
    fun unpack(input: InputStream, into: File, maxBytes: Long = MAX_BYTES): Info {
        into.deleteRecursively()
        into.mkdirs()
        try {
            var total = 0L
            val buffer = ByteArray(64 * 1024)
            ZipInputStream(input.buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    if (entry.isDirectory) continue
                    if (!allowed(entry.name)) throw NotABackup()
                    val f = File(into, entry.name)
                    f.parentFile?.mkdirs()
                    f.outputStream().use { o ->
                        while (true) {
                            val n = zip.read(buffer)
                            if (n < 0) break
                            total += n
                            if (total > maxBytes) throw NotABackup()
                            o.write(buffer, 0, n)
                        }
                    }
                }
            }
            val info = File(into, INFO).takeIf { it.isFile }?.let { Info.parse(it.readText()) } ?: throw NotABackup()
            if (info.version !in 1..VERSION) throw NotABackup()
            for (name in PREFS) File(into, "prefs/$name.json").takeIf { it.isFile }?.let { readInts(it.readText()) }
            File(into, "boxart/choices.json").takeIf { it.isFile }?.let { readChoices(it.readText()) }
            return info
        } catch (e: Exception) {
            into.deleteRecursively()
            throw NotABackup()
        }
    }

    /** A preference file's values, `{"name": 1}`; throws on anything else. */
    fun readInts(text: String): Map<String, Int> {
        val r = Json.Reader(text)
        val m = r.value() as Map<*, *>
        r.end()
        return m.entries.associate { (k, v) ->
            val d = v as Double
            require(d == Math.rint(d) && d >= Int.MIN_VALUE && d <= Int.MAX_VALUE)
            k as String to d.toInt()
        }
    }

    /** The box-art choices, `{"<id>": "own" | "removed"}`; throws on anything else. */
    fun readChoices(text: String): Map<String, BoxArt.State> {
        val r = Json.Reader(text)
        val m = r.value() as Map<*, *>
        r.end()
        return m.entries.associate { (k, v) ->
            require(Regex(ID).matches(k as String))
            k to when (v) {
                "own" -> BoxArt.State.OWN
                "removed" -> BoxArt.State.REMOVED
                else -> error("not a choice: $v")
            }
        }
    }

    private fun intsJson(values: Map<String, Int>): String =
        values.toSortedMap().entries.joinToString(", ", "{", "}") { (k, v) -> "${Json.str(k)}: $v" }

    private fun choicesJson(choices: Map<String, BoxArt.State>): String =
        choices.toSortedMap().entries.joinToString(", ", "{", "}") { (k, v) -> "${Json.str(k)}: ${Json.str(v.name.lowercase())}" }

    /** A size for the note: "740 KB", "12.0 MB". */
    fun sizeText(bytes: Long): String =
        if (bytes < 1L shl 20) "${(bytes + 1023) / 1024} KB" else String.format(Locale.US, "%.1f MB", bytes / 1048576.0)
}
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 163, 0 failures.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Backup.kt app/src/test/java/com/tengen/gbaemu/BackupTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-11-backup.md
git commit -m "feat: the backup file (Backup)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Restoring (`Restore`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/Restore.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/RestoreTest.kt` (new, 6 tests)

- [x] **Step 1: Write the failing test**

```kotlin
package com.tengen.gbaemu

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RestoreTest {
    private val id = "e86b3224"
    private val other = "1f1c08fb"

    private inner class Phone(name: String, failAt: String? = null) {
        val dir: File = Files.createTempDirectory(name).toFile()
        val prefs = Backup.PREFS.associateWith { MemoryStore() }
        val art = BoxArt(File(dir, "boxart"))
        val restore = Restore(dir, prefs, art) { part -> if (part == failAt) error("disk trouble at $part") }
        fun put(path: String, text: String) = File(dir, path).apply { parentFile?.mkdirs(); writeText(text) }
        fun backup(): ByteArray = ByteArrayOutputStream().also { restore.backUp(it, "0.1", "Pixel 9", 1000) }.toByteArray()

        /** The parts a backup holds, as path → text. */
        fun tree(): Map<String, String> = dir.walkTopDown().filter { it.isFile }
            .map { it.relativeTo(dir).invariantSeparatorsPath to it.readText() }
            .filter { (p, _) -> listOf("saves/", "states/", "cheats/", "skins/", "layout.json").any { p.startsWith(it) } }
            .toMap().toSortedMap()

        fun settings() = prefs.mapValues { it.value.all() }
    }

    private fun full(p: Phone) {
        p.put("saves/$id.sav", "save")
        p.put("states/$id/auto.state", "auto")
        p.put("states/$id/auto.png", "picture")
        p.put("states/$id/slot1.state", "one")
        p.put("cheats/$id.json", "[]")
        p.put("layout.json", "{\"a\": 1}")
        p.put("skins/1/info.json", "{}")
        p.put("skins/1/sub/p.png", "png")
        p.art.own("cccccccc", byteArrayOf(5))
        p.art.remove("dddddddd")
        p.prefs.getValue("settings").put("smooth", 1)
        p.prefs.getValue("keys").put("k", 42)
        p.prefs.getValue("game_settings").put("$id.save", 3)
    }

    private fun restore(to: Phone, bytes: ByteArray): Restore.Result {
        assertTrue(to.restore.check(ByteArrayInputStream(bytes)) != null)
        return to.restore.apply("0.1", "Pixel 9", 2000)
    }

    @Test
    fun backUpThenRestoreOnAnEmptyPhoneGivesEverythingBack() {
        val a = Phone("a")
        full(a)
        val b = Phone("b")
        val info = b.restore.check(ByteArrayInputStream(a.backup()))!!
        assertEquals(listOf(1, 2, 1, 1), listOf(info.games, info.states, info.cheats, info.skins))
        assertTrue(b.restore.apply("0.1", "Pixel 9", 2000).ok)
        assertEquals(a.tree(), b.tree())
        assertEquals(a.settings(), b.settings())
        assertEquals(a.art.choices(), b.art.choices())
        assertEquals(5.toByte(), b.art.picture("cccccccc")!!.readBytes().single())
        assertFalse(File(b.dir, "restore/incoming").exists())
    }

    @Test
    fun restoringReplacesButKeepsDownloadedBoxArt() {
        val a = Phone("a")
        full(a)
        val b = Phone("b")
        b.put("saves/$other.sav", "theirs")
        b.put("states/$other/slot3.state", "theirs")
        b.art.found("aaaaaaaa", "A", byteArrayOf(1), 1)
        b.art.own("eeeeeeee", byteArrayOf(2))
        b.prefs.getValue("settings").put("sound", 0)
        assertTrue(restore(b, a.backup()).ok)
        assertEquals(a.tree(), b.tree()) // the other game's save and state are gone
        assertEquals(mapOf("smooth" to 1), b.prefs.getValue("settings").all())
        assertTrue(b.art.picture("aaaaaaaa")!!.isFile)
        assertNull(b.art.entry("eeeeeeee"))
    }

    @Test
    fun aFailureHalfWayPutsThePhoneBack() {
        val a = Phone("a")
        full(a)
        val b = Phone("b", failAt = "cheats")
        b.put("saves/$other.sav", "theirs")
        b.put("cheats/$other.json", "[1]")
        b.prefs.getValue("keys").put("k", 7)
        val before = b.tree()
        val r = restore(b, a.backup())
        assertFalse(r.ok)
        assertEquals(before, b.tree())
        assertEquals(mapOf("k" to 7), b.prefs.getValue("keys").all())
    }

    @Test
    fun undoPutsBackWhatWasThereOnce() {
        val a = Phone("a")
        full(a)
        val b = Phone("b")
        b.put("saves/$other.sav", "theirs")
        b.prefs.getValue("keys").put("k", 7)
        val before = b.tree()
        assertTrue(restore(b, a.backup()).ok)
        assertTrue(b.restore.safety.isFile)
        assertTrue(b.restore.undo().ok)
        assertEquals(before, b.tree())
        assertEquals(mapOf("k" to 7), b.prefs.getValue("keys").all())
        assertFalse(b.restore.safety.exists())
        assertFalse(b.restore.undo().ok)
    }

    @Test
    fun theBiosSettingIsSwitchedOffWithoutABiosFile() {
        val a = Phone("a")
        a.prefs.getValue("settings").put("bios_file", 1)
        val bytes = a.backup()
        val b = Phone("b")
        assertTrue(restore(b, bytes).biosOff)
        assertEquals(0, b.prefs.getValue("settings").get("bios_file"))
        val c = Phone("c")
        c.put("bios/gba_bios.bin", "x")
        assertFalse(restore(c, bytes).biosOff)
        assertEquals(1, c.prefs.getValue("settings").get("bios_file"))
        assertTrue(File(c.dir, "bios/gba_bios.bin").isFile) // the BIOS file itself is never touched
    }

    @Test
    fun aFileThatIsNotABackupChangesNothing() {
        val b = Phone("b")
        b.put("saves/$other.sav", "theirs")
        assertNull(b.restore.check(ByteArrayInputStream("hello".toByteArray())))
        assertFalse(b.restore.apply("0.1", "Pixel 9", 1).ok) // nothing checked
        assertEquals(mapOf("saves/$other.sav" to "theirs"), b.tree())
        assertFalse(b.restore.safety.exists())
    }
}
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile error (`Restore` unknown).

- [x] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/Restore.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream

/**
 * Backing up and restoring the phone's data (11, spec §4): [check] unpacks a backup into `files/restore/incoming`,
 * [apply] writes a safety copy (`files/restore/safety.zip`) and then replaces each part; if a step fails, the safety
 * copy is put back the same way. [undo] puts the safety copy back once. [prefs]: the three preference files by name.
 * [beforePart] is called before each part is replaced (tests make it fail).
 */
class Restore(private val filesDir: File, private val prefs: Map<String, IntStore>, private val boxArt: BoxArt,
              private val beforePart: (String) -> Unit = {}) {
    private val work = File(filesDir, "restore")
    private val incoming = File(work, "incoming")
    val safety = File(work, "safety.zip")

    /** [biosOff]: the backup had the BIOS file switched on, but this phone has no BIOS file, so it was switched off. */
    class Result(val ok: Boolean, val biosOff: Boolean)

    fun source() = Backup.Source(filesDir, Backup.PREFS.associateWith { prefs[it]?.all().orEmpty() }, boxArt.choices())

    /** A backup of the phone's data into [out]; its size in bytes. */
    fun backUp(out: OutputStream, app: String, phone: String, now: Long): Long {
        val counting = Backup.Counting(out)
        Backup.write(source(), counting, app, phone, now)
        counting.flush()
        return counting.count
    }

    /** Checks and unpacks [input] for [apply]; null: not a backup this app can restore (nothing changed). */
    fun check(input: InputStream): Backup.Info? = try {
        Backup.unpack(input, incoming)
    } catch (e: Backup.NotABackup) {
        null
    }

    /** The checked backup is dropped (the user said Cancel). */
    fun discard() {
        incoming.deleteRecursively()
    }

    /** The checked backup replaces the phone's data, after a safety copy. */
    fun apply(app: String, phone: String, now: Long): Result {
        if (!incoming.isDirectory) return Result(false, false)
        try {
            val tmp = File(work, "safety.zip.tmp")
            tmp.outputStream().use { backUp(it, app, phone, now) }
            safety.delete()
            if (!tmp.renameTo(safety)) throw IOException("couldn't keep the safety copy")
        } catch (e: Exception) {
            discard()
            return Result(false, false)
        }
        return swapOrRollBack()
    }

    /** Puts the safety copy back (Undo restore); then it is deleted. */
    fun undo(): Result {
        if (!safety.isFile || safety.inputStream().use { check(it) } == null) return Result(false, false)
        val r = swapOrRollBack()
        if (r.ok) safety.delete()
        return r
    }

    private fun swapOrRollBack(): Result = try {
        Result(true, swap(incoming, beforePart))
    } catch (e: Exception) {
        try {
            val back = File(work, "rollback")
            safety.inputStream().use { Backup.unpack(it, back) }
            swap(back) {}
            back.deleteRecursively()
        } catch (x: Exception) { // nothing more can be done
        }
        Result(false, false)
    } finally {
        discard()
    }

    /** Replaces each part with [from]'s; true if the BIOS file setting had to be switched off. */
    private fun swap(from: File, before: (String) -> Unit): Boolean {
        for (part in listOf("saves", "states", "cheats", "skins")) {
            before(part)
            val target = File(filesDir, part)
            target.deleteRecursively()
            File(from, part).takeIf { it.isDirectory }?.let { move(it, target) }
        }
        before("layout.json")
        val layout = File(filesDir, "layout.json")
        layout.delete()
        File(from, "layout.json").takeIf { it.isFile }?.let { move(it, layout) }
        before("boxart")
        val choices = File(from, "boxart/choices.json").takeIf { it.isFile }?.let { Backup.readChoices(it.readText()) }.orEmpty()
        val pictures = choices.keys.mapNotNull { id -> File(from, "boxart/$id.jpg").takeIf { it.isFile }?.let { id to it.readBytes() } }.toMap()
        boxArt.replaceChoices(choices, pictures)
        before("prefs")
        for (name in Backup.PREFS) {
            val store = prefs[name] ?: continue
            store.clear()
            File(from, "prefs/$name.json").takeIf { it.isFile }?.let { Backup.readInts(it.readText()) }?.forEach { (k, v) -> store.put(k, v) }
        }
        val settings = prefs["settings"]
        if (settings?.get("bios_file") == 1 && !Bios.file(filesDir).isFile) {
            settings.put("bios_file", 0)
            return true
        }
        return false
    }

    private fun move(from: File, to: File) {
        to.parentFile?.mkdirs()
        if (!from.renameTo(to)) {
            from.copyRecursively(to, overwrite = true)
            from.deleteRecursively()
        }
    }
}
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 169, 0 failures.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Restore.kt app/src/test/java/com/tengen/gbaemu/RestoreTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-11-backup.md
git commit -m "feat: restoring a backup with a safety copy (Restore)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: When to back up by itself (`BackupSchedule`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/BackupSchedule.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/BackupScheduleTest.kt` (new, 4 tests)

- [x] **Step 1: Write the failing test**

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BackupScheduleTest {
    private val file = File(Files.createTempDirectory("schedule").toFile(), "backup-state.json")

    @Test
    fun nothingStoredMeansNoFolderAndNoBackupYet() {
        val s = BackupSchedule(file)
        assertNull(s.folder)
        assertEquals(0L, s.lastAuto)
        assertEquals(0L, s.lastBackup)
        file.writeText("{")
        assertNull(BackupSchedule(file).folder) // damaged: as new
    }

    @Test
    fun itIsSavedAndReadBack() {
        val s = BackupSchedule(file)
        s.folder = "content://tree/primary%3ABackups"
        s.lastAuto = 5
        s.lastBackup = 9
        s.save()
        val t = BackupSchedule(file)
        assertEquals("content://tree/primary%3ABackups", t.folder)
        assertEquals(5L, t.lastAuto)
        assertEquals(9L, t.lastBackup)
        t.folder = null
        t.save()
        assertNull(BackupSchedule(file).folder)
    }

    @Test
    fun anAutomaticBackupIsDueOnlyWithAFolderAndADayAfterTheLast() {
        val s = BackupSchedule(file)
        assertFalse(s.due(BackupSchedule.DAY * 10))
        s.folder = "content://x"
        assertTrue(s.due(BackupSchedule.DAY * 10))
        s.lastAuto = BackupSchedule.DAY * 10
        assertFalse(s.due(BackupSchedule.DAY * 11 - 1))
        assertTrue(s.due(BackupSchedule.DAY * 11))
    }

    @Test
    fun theNamesAreOursAndOnlyOurOldestBeyondFiveAreDeleted() {
        val name = BackupSchedule.fileName(1_759_600_000_000L)
        assertTrue(name, BackupSchedule.OURS.matches(name))
        assertTrue(name.startsWith("GBA Emulator backup 20") && name.endsWith(".zip"))
        val ours = (1..7).map { "GBA Emulator backup 2026-10-0$it 10-00.zip" }
        val names = ours + listOf("GBA Emulator backup 2026-10-01 10-00 (1).zip", "holiday.jpg", "GBA Emulator backup notes.zip")
        assertEquals(listOf("GBA Emulator backup 2026-10-02 10-00.zip", "GBA Emulator backup 2026-10-01 10-00.zip",
            "GBA Emulator backup 2026-10-01 10-00 (1).zip").sorted(), BackupSchedule.toDelete(names.shuffled()).sorted())
    }
}
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile error (`BackupSchedule` unknown).

- [x] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/BackupSchedule.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Automatic backups and the Backup row's time (11), in `files/backup-state.json` (not part of a backup: the folder's
 * permission is this phone's): the picked [folder] (null: automatic backup off), when the last automatic backup and
 * the last backup of any kind were made; when one is due, and which of the folder's files to delete.
 */
class BackupSchedule(private val file: File) {
    var folder: String? = null
    var lastAuto = 0L
    var lastBackup = 0L

    init {
        try {
            val m = Json.Reader(file.readText()).value() as Map<*, *>
            folder = m["folder"] as String?
            lastAuto = (m["lastAuto"] as Double).toLong()
            lastBackup = (m["lastBackup"] as Double).toLong()
        } catch (e: Exception) { // none yet, or damaged: as new
            folder = null
            lastAuto = 0
            lastBackup = 0
        }
    }

    fun save() {
        file.parentFile?.mkdirs()
        file.writeText("{\"folder\": ${folder?.let(Json::str) ?: "null"}, \"lastAuto\": $lastAuto, \"lastBackup\": $lastBackup}")
    }

    /** An automatic backup is due: a folder is picked and the last one is [DAY] or more ago. */
    fun due(now: Long): Boolean = folder != null && now - lastAuto >= DAY

    companion object {
        const val DAY = 24L * 60 * 60 * 1000
        const val KEEP = 5

        /** The name of a backup made at [now]: `GBA Emulator backup 2026-10-04 14-02.zip`. */
        fun fileName(now: Long): String =
            "GBA Emulator backup " + SimpleDateFormat("yyyy-MM-dd HH-mm", Locale.US).format(Date(now)) + ".zip"

        /** The app's own backup names (Android may add " (1)" when the name is taken). */
        val OURS = Regex("GBA Emulator backup \\d{4}-\\d{2}-\\d{2} \\d{2}-\\d{2}( \\(\\d+\\))?\\.zip")

        /** Of a folder's file names, the app's own beyond the newest [KEEP] (to delete); other files never. */
        fun toDelete(names: List<String>): List<String> = names.filter { OURS.matches(it) }.sortedDescending().drop(KEEP)
    }
}
```

(**Wrong — fixed in Task 7.** Claude's check showed the text sort deletes the *newer* of two same-minute backups: " (1)" sorts before ".zip", and "(10)" before "(9)". `toDelete` now sorts by the minute, then the " (n)" number as a number; `BackupScheduleTest` +1.)

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 173, 0 failures.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/BackupSchedule.kt app/src/test/java/com/tengen/gbaemu/BackupScheduleTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-11-backup.md
git commit -m "feat: when to back up by itself (BackupSchedule)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The screens — the Backup page, the restore question, the spinner

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt`, `app/src/main/java/com/tengen/gbaemu/Screens.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/BackupTextTest.kt` (new, 2), `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt` (+3)

- [x] **Step 1: Write the failing tests**

Create `BackupTextTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class BackupTextTest {
    @Test
    fun theBackupRowSaysTheLastBackupAndAutomatic() {
        assertEquals("No backup yet", backupSummary(BackupStatus()))
        assertEquals("No backup yet · automatic", backupSummary(BackupStatus(auto = true)))
        val today = backupSummary(BackupStatus(lastBackup = System.currentTimeMillis()))
        assertTrue(today, today.startsWith("Last backup today "))
    }

    @Test
    fun theRestoreQuestionCountsWhatIsInTheBackup() {
        val many = Backup.Info(1, "0.1", 0, "phone A", games = 6, states = 23, cheats = 4, skins = 2)
        assertEquals("Made on phone A, 4 Oct 2026 14:02: 6 games, 23 save states, settings, cheats, 2 skins. " +
            "Everything on this phone is replaced; what's here now is kept as a safety copy.", restoreText(many, "4 Oct 2026 14:02"))
        val one = Backup.Info(1, "0.1", 0, "Pixel 9", games = 1, states = 1, cheats = 0, skins = 1)
        assertEquals("Made on Pixel 9, today: 1 game, 1 save state, settings, 1 skin. " +
            "Everything on this phone is replaced; what's here now is kept as a safety copy.", restoreText(one, "today"))
    }
}
```

In `ScreensTest.kt` add the imports `import androidx.compose.ui.test.assertIsNotEnabled` and the tests:

```kotlin
    @Test
    fun theBackupPageBacksUpRestoresUndoesAndSetsAutomatic() {
        val did = mutableListOf<String>()
        compose.setContent {
            AppTheme {
                SettingsScreen(SettingsPage.BACKUP, Settings(), KeyBindings(MemoryStore()), 0, "Built-in",
                    onPage = {}, onBack = {}, onChange = {}, onPickKey = { _, _ -> }, onResetKeys = {},
                    onChooseBios = {}, onBuiltInBios = {},
                    backup = BackupStatus(auto = true, folderName = "Backups", canUndo = true),
                    onBackUp = { did += "backup" }, onRestore = { did += "restore" }, onUndoRestore = { did += "undo" },
                    onAutoBackup = { did += "auto $it" }, onChangeFolder = { did += "folder" })
            }
        }
        compose.onNodeWithText("Back up now").performClick()
        compose.onNodeWithText("Restore from backup").performClick()
        compose.onNodeWithText("Undo restore").performClick()
        compose.onNodeWithText("Automatic backup").performScrollTo().performClick()
        compose.onNodeWithText("Backups").performScrollTo().assertIsDisplayed()
        compose.onNodeWithText("Change folder").performScrollTo().performClick()
        assertEquals(listOf("backup", "restore", "undo", "auto false", "folder"), did)
    }

    @Test
    fun duringAGameTheBackupButtonsAreOff() {
        compose.setContent {
            AppTheme {
                SettingsScreen(SettingsPage.BACKUP, Settings(), KeyBindings(MemoryStore()), 0, "Built-in",
                    onPage = {}, onBack = {}, onChange = {}, onPickKey = { _, _ -> }, onResetKeys = {},
                    onChooseBios = {}, onBuiltInBios = {}, backup = BackupStatus(inGame = true, canUndo = true))
            }
        }
        compose.onNodeWithText("Quit the game to back up or restore.").assertIsDisplayed()
        compose.onNodeWithText("Back up now").assertIsNotEnabled()
        compose.onNodeWithText("Restore from backup").assertIsNotEnabled()
        compose.onNodeWithText("Undo restore").assertIsNotEnabled()
    }

    @Test
    fun theRestoreQuestionRestoresOrCancels() {
        var restored = 0
        var cancelled = 0
        compose.setContent {
            AppTheme { RestoreDialog("Made on Pixel 9, today: 1 game.", onRestore = { restored++ }, onCancel = { cancelled++ }) }
        }
        compose.onNodeWithText("Restore this backup?").assertIsDisplayed()
        compose.onNodeWithText("Made on Pixel 9, today: 1 game.").assertIsDisplayed()
        compose.onNodeWithText("Restore").performClick()
        compose.onNodeWithText("Cancel").performClick()
        assertEquals(1, restored)
        assertEquals(1, cancelled)
    }
```

- [x] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile errors (`BackupStatus`, `backupSummary`, `restoreText` unknown).

- [x] **Step 3: Write the code**

`SettingsScreens.kt`: imports `import androidx.compose.material.icons.filled.Backup` and `import androidx.compose.material.icons.filled.Folder`; in `SettingsPage` add `BACKUP("Backup")` after `SKIN("Skin")`. Above `SettingsScreen`:

```kotlin
/** What the Backup page shows (11): the last backup (0 = none), automatic backup and its folder, Undo, and whether a game is open. */
data class BackupStatus(val lastBackup: Long = 0, val auto: Boolean = false, val folderName: String? = null,
                        val canUndo: Boolean = false, val inGame: Boolean = false)

/** The Backup row's summary: "Last backup today 14:02", "No backup yet", with " · automatic" when it is on. */
fun backupSummary(status: BackupStatus): String =
    (if (status.lastBackup > 0) "Last backup " + playedText(status.lastBackup).replaceFirstChar { it.lowercase() } else "No backup yet") +
        if (status.auto) " · automatic" else ""
```

`SettingsScreen`'s parameters: after `onRemoveSkin: (Int) -> Unit = {}` add
`, backup: BackupStatus = BackupStatus(), onBackUp: () -> Unit = {}, onRestore: () -> Unit = {}, onUndoRestore: () -> Unit = {}, onAutoBackup: (Boolean) -> Unit = {}, onChangeFolder: () -> Unit = {}`.

On the MAIN page after the BIOS `NavRow`:

```kotlin
                        NavRow(Icons.Filled.Backup, "Backup", backupSummary(backup)) { onPage(SettingsPage.BACKUP) }
```

and a page before `SettingsPage.BIOS -> {`:

```kotlin
                    SettingsPage.BACKUP -> {
                        if (backup.inGame) {
                            Text("Quit the game to back up or restore.", color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(vertical = 12.dp))
                        }
                        Button(onClick = onBackUp, enabled = !backup.inGame, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                            Text("Back up now")
                        }
                        Spacer(Modifier.height(8.dp))
                        OutlinedButton(onClick = onRestore, enabled = !backup.inGame, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                            Text("Restore from backup")
                        }
                        if (backup.canUndo) {
                            Spacer(Modifier.height(8.dp))
                            OutlinedButton(onClick = onUndoRestore, enabled = !backup.inGame, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                                Text("Undo restore")
                            }
                        }
                        Help("One file with your saves, save states, settings, controls, cheats, imported skins and chosen box art. Not your BIOS file or the games.")
                        Spacer(Modifier.height(8.dp))
                        SwitchRow("Automatic backup", backup.auto, onAutoBackup,
                            help = "After you quit a game, at most once a day, a backup goes into a folder you pick. The newest 5 are kept.")
                        if (backup.auto) NavRow(Icons.Filled.Folder, "Change folder", backup.folderName ?: "The picked folder") { onChangeFolder() }
                        Help("Backups stay where you put them. The app doesn't send them anywhere, and Android's own backup is off for this app.")
                    }
```

`Screens.kt`, after `RestartDialog`:

```kotlin
/** The restore question's text (11): what the backup holds, then what restoring does. [date]: when it was made, as shown. */
fun restoreText(info: Backup.Info, date: String): String {
    fun count(n: Int, one: String) = "$n $one" + if (n == 1) "" else "s"
    val parts = listOf(count(info.games, "game"), count(info.states, "save state"), "settings") +
        listOfNotNull("cheats".takeIf { info.cheats > 0 }, count(info.skins, "skin").takeIf { info.skins > 0 })
    return "Made on ${info.phone}, $date: ${parts.joinToString(", ")}. " +
        "Everything on this phone is replaced; what's here now is kept as a safety copy."
}

/** A checked backup waits for the user's yes (11). */
@Composable
fun RestoreDialog(text: String, onRestore: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Restore this backup?") },
        text = { Text(text) },
        confirmButton = { DangerButton("Restore", onRestore) },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** Undo restore asks first (11). */
@Composable
fun UndoRestoreDialog(onUndo: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Undo the last restore?") },
        text = { Text("What was on this phone before the last restore is put back.") },
        confirmButton = { DangerButton("Undo restore", onUndo) },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** A spinner while a backup is made, checked or restored (11); it can't be closed. */
@Composable
fun BusyDialog(text: String) {
    AlertDialog(
        onDismissRequest = {},
        text = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(Modifier.size(24.dp))
                Spacer(Modifier.width(12.dp))
                Text(text)
            }
        },
        confirmButton = {},
    )
}
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 175, 0 failures.
Run: `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest` — Expected: on-device 73, 0 failures. (If `SessionTest.runsAtGbaSpeed` alone fails by a few frames, rerun once — 10b's notes: the emulator short of memory; the core is untouched.)

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/test/java/com/tengen/gbaemu/BackupTextTest.kt app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-11-backup.md
git commit -m "feat: the Backup page, the restore question and the spinner

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The folder, the wiring and Android's backup off

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/BackupFolder.kt`, `app/src/main/res/xml/data_extraction_rules.xml`
- Modify: `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`, `app/src/main/AndroidManifest.xml`
- Test: `app/src/androidTest/java/com/tengen/gbaemu/BackupDeviceTest.kt` (new, 1)

- [x] **Step 1: Write the failing on-device test** (real `SharedPreferences` through `PrefsStore`)

```kotlin
package com.tengen.gbaemu

import android.content.Context
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** A backup and a restore on the phone, with Android's real preference files. */
@RunWith(AndroidJUnit4::class)
class BackupDeviceTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val dir = File(context.filesDir, "test_backup")
    private val names = Backup.PREFS.associateWith { "test_backup_$it" }

    @After
    fun cleanUp() {
        dir.deleteRecursively()
        for (n in names.values) context.deleteSharedPreferences(n)
    }

    @Test
    fun aBackupComesBackAfterChanges() {
        val prefs = names.mapValues { (_, n) -> PrefsStore(context.getSharedPreferences(n, Context.MODE_PRIVATE)) as IntStore }
        val restore = Restore(dir, prefs, BoxArt(File(dir, "boxart")))
        File(dir, "saves").mkdirs()
        File(dir, "saves/e86b3224.sav").writeText("save")
        prefs.getValue("settings").put("smooth", 1)
        prefs.getValue("keys").put("k", 42)
        val bytes = ByteArrayOutputStream().also { restore.backUp(it, "0.1", "test", 1) }.toByteArray()
        File(dir, "saves/e86b3224.sav").writeText("changed")
        prefs.getValue("settings").put("smooth", 0)
        prefs.getValue("keys").put("other", 1)
        assertTrue(restore.check(ByteArrayInputStream(bytes)) != null)
        assertTrue(restore.apply("0.1", "test", 2).ok)
        assertEquals("save", File(dir, "saves/e86b3224.sav").readText())
        assertEquals(mapOf("smooth" to 1), prefs.getValue("settings").all())
        assertEquals(mapOf("k" to 42), prefs.getValue("keys").all())
    }
}
```

(It compiles already — `Restore` exists — so it is the "proof on the phone" test, run in Step 5.)

- [x] **Step 2: `BackupFolder.kt`**

```kotlin
package com.tengen.gbaemu

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document

/** The automatic backups' folder (11): a folder the user picked (Android's document tree); every call may throw. */
class BackupFolder(private val resolver: ContentResolver, private val tree: Uri) {
    private val folder: Uri = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))

    /** The folder's name as the picker showed it. */
    fun name(): String? = resolver.query(folder, arrayOf(Document.COLUMN_DISPLAY_NAME), null, null, null)?.use {
        if (it.moveToFirst()) it.getString(0) else null
    }

    /** A new, empty `.zip` called [name] in the folder. */
    fun create(name: String): Uri? = DocumentsContract.createDocument(resolver, folder, "application/zip", name)

    /** The folder's files: name and address. */
    fun list(): List<Pair<String, Uri>> {
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
        return resolver.query(children, arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME), null, null, null)?.use { c ->
            val out = mutableListOf<Pair<String, Uri>>()
            while (c.moveToNext()) out += c.getString(1) to DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0))
            out
        }.orEmpty()
    }

    fun delete(doc: Uri): Boolean = try {
        DocumentsContract.deleteDocument(resolver, doc)
    } catch (e: Exception) {
        false
    }
}
```

- [x] **Step 3: Android's own backup off**

Create `app/src/main/res/xml/data_extraction_rules.xml` (Android 12+: neither cloud backup nor device-to-device transfer takes the app's data — `allowBackup="false"` alone doesn't stop the transfer there):

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- Milestone 11: the app's data leaves the phone only in the user's own backups. -->
<data-extraction-rules>
    <cloud-backup>
        <exclude domain="root" path="." />
        <exclude domain="file" path="." />
        <exclude domain="database" path="." />
        <exclude domain="sharedpref" path="." />
        <exclude domain="external" path="." />
    </cloud-backup>
    <device-transfer>
        <exclude domain="root" path="." />
        <exclude domain="file" path="." />
        <exclude domain="database" path="." />
        <exclude domain="sharedpref" path="." />
        <exclude domain="external" path="." />
    </device-transfer>
</data-extraction-rules>
```

In `AndroidManifest.xml`, the `<application` element gets:

```xml
        android:allowBackup="false"
        android:dataExtractionRules="@xml/data_extraction_rules"
```

(after `android:label="GBA Emulator"`), with the comment above `<application`: `<!-- Android's own backup off (11): the app's data leaves the phone only in the user's own backups. -->`.

- [x] **Step 4: The wiring in `MainActivity.kt`**

Imports to add: `import android.provider.DocumentsContract`, `import java.text.DateFormat`, `import java.util.Date`.

Fields after `private var onlineCheats …`:

```kotlin
    private lateinit var backups: Restore // backing up and restoring (11)
    private lateinit var backupSchedule: BackupSchedule
    private var backupStatus by mutableStateOf(BackupStatus())
    private var busy by mutableStateOf<String?>(null) // the spinner's text while a backup is made, checked or restored
    private var restoreInfo by mutableStateOf<Backup.Info?>(null) // a checked backup waiting for the user's yes
    private var askUndo by mutableStateOf(false)
    private var autoRunning = false
    private var autoFailSaid = false // "Automatic backup failed" is said once per run
```

Launchers after `pickBox`:

```kotlin
    private val backupTo = registerForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri ->
        if (uri != null) backUpTo(uri)
    }
    private val pickBackup = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) checkBackup(uri)
    }
    private val pickBackupFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) useBackupFolder(uri)
    }
```

In `onCreate`, replace

```kotlin
        keys = KeyBindings(PrefsStore(getSharedPreferences("keys", MODE_PRIVATE)))
        keys.use()
        gameSettings = GameSettings(PrefsStore(getSharedPreferences("game_settings", MODE_PRIVATE)))
```

with

```kotlin
        val keysStore = PrefsStore(getSharedPreferences("keys", MODE_PRIVATE))
        keys = KeyBindings(keysStore)
        keys.use()
        val gameStore = PrefsStore(getSharedPreferences("game_settings", MODE_PRIVATE))
        gameSettings = GameSettings(gameStore)
        backups = Restore(filesDir, mapOf("settings" to settingsStore, "keys" to keysStore, "game_settings" to gameStore), boxArt)
        backupSchedule = BackupSchedule(File(filesDir, "backup-state.json"))
        refreshBackupStatus()
```

In `setContent`, after the `onlineCheats?.let { … }` line:

```kotlin
                restoreInfo?.let { info ->
                    RestoreDialog(restoreText(info, DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(info.made))),
                        onRestore = ::restoreNow, onCancel = { restoreInfo = null; backups.discard() })
                }
                if (askUndo) UndoRestoreDialog(onUndo = ::undoRestore, onCancel = { askUndo = false })
                busy?.let { BusyDialog(it) }
```

The `SettingsScreen(...)` call's last line becomes:

```kotlin
                        onImportSkin = { pickSkin.launch(arrayOf("*/*")) }, onRemoveSkin = ::removeSkin,
                        backup = backupStatus.copy(inGame = playing),
                        onBackUp = { backupTo.launch(BackupSchedule.fileName(System.currentTimeMillis())) },
                        onRestore = { pickBackup.launch(arrayOf("*/*")) },
                        onUndoRestore = { askUndo = true },
                        onAutoBackup = { on -> if (on) pickBackupFolder.launch(null) else stopAutoBackup() },
                        onChangeFolder = { pickBackupFolder.launch(null) })
```

`quitGame()`: add `autoBackup()` as its last line (after `refreshLibrary()`; the save and the Continue state are written by then).

The backup helpers (after `pickOnlineCheats`):

```kotlin
    /** The phone's name as the user set it, written into backups. */
    private fun phoneName(): String =
        android.provider.Settings.Global.getString(contentResolver, android.provider.Settings.Global.DEVICE_NAME) ?: android.os.Build.MODEL

    private fun appVersion(): String = try {
        packageManager.getPackageInfo(packageName, 0).versionName ?: ""
    } catch (e: Exception) {
        ""
    }

    private fun refreshBackupStatus() {
        val folder = backupSchedule.folder
        backupStatus = BackupStatus(backupSchedule.lastBackup, auto = folder != null,
            folderName = folder?.let {
                try {
                    BackupFolder(contentResolver, Uri.parse(it)).name()
                } catch (e: Exception) {
                    null
                }
            },
            canUndo = backups.safety.isFile)
    }

    /** Back up now: the whole backup into the file the user picked, off the UI thread. */
    private fun backUpTo(uri: Uri) {
        busy = "Backing up"
        val app = appVersion()
        val phone = phoneName()
        thread(name = "backup") {
            val now = System.currentTimeMillis()
            val size = try {
                contentResolver.openOutputStream(uri, "wt")?.use { backups.backUp(it, app, phone, now) } ?: -1L
            } catch (e: Exception) {
                -1L
            }
            if (size < 0) try {
                DocumentsContract.deleteDocument(contentResolver, uri)
            } catch (e: Exception) {
            }
            runOnUiThread {
                busy = null
                if (size < 0) toast("Couldn't save the backup.") else {
                    backupSchedule.lastBackup = now
                    backupSchedule.save()
                    refreshBackupStatus()
                    toast("Backup saved (${Backup.sizeText(size)}).")
                }
            }
        }
    }

    /** Restore from backup: the picked file is checked (and unpacked) first; then the question. */
    private fun checkBackup(uri: Uri) {
        busy = "Checking the backup"
        thread(name = "restore") {
            val info = try {
                contentResolver.openInputStream(uri)?.use { backups.check(it) }
            } catch (e: Exception) {
                null
            }
            runOnUiThread {
                busy = null
                if (info == null) toast("That file isn't a backup this app can restore.") else restoreInfo = info
            }
        }
    }

    private fun restoreNow() {
        restoreInfo = null
        val app = appVersion()
        val phone = phoneName()
        runRestore("Restored.") { backups.apply(app, phone, System.currentTimeMillis()) }
    }

    private fun undoRestore() {
        askUndo = false
        runRestore("Put back what was here before.") { backups.undo() }
    }

    /** A restore (or Undo) off the UI thread, with the box-art lookups stopped; then everything is read again. */
    private fun runRestore(done: String, job: () -> Restore.Result) {
        stopBoxArt()
        busy = "Restoring"
        thread(name = "restore") {
            val r = job()
            runOnUiThread {
                busy = null
                reloadAll()
                toast(if (r.ok) done else "Couldn't restore; nothing was changed.")
                if (r.biosOff) toast("Choose your BIOS file again in Settings → BIOS.")
            }
        }
    }

    /** After a restore: settings, keys, layouts, skins, the Backup page and the list's pictures read again. */
    private fun reloadAll() {
        settings = Settings.load(settingsStore)
        fastSpeed = settings.fastSpeed
        applySettings(settings)
        keys.use()
        keysChanged++
        layouts = layoutStore.load()
        refreshSkins()
        if (settings.skinImport != 0 && activeSkin() == null) changeSettings(settings.copy(skinImport = 0))
        refreshBackupStatus()
        refreshLibrary()
        libraryRefresh++
    }

    /** The automatic backups' folder picked (or changed): its permission kept, the old one's given back. */
    private fun useBackupFolder(tree: Uri) {
        try {
            contentResolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        backupSchedule.folder?.let { old -> if (old != tree.toString()) releaseBackupFolder(old) }
        backupSchedule.folder = tree.toString()
        backupSchedule.save()
        refreshBackupStatus()
    }

    private fun stopAutoBackup() {
        backupSchedule.folder?.let(::releaseBackupFolder)
        backupSchedule.folder = null
        backupSchedule.save()
        refreshBackupStatus()
    }

    /** Gives the folder's permission back, unless it is also the games folder (that one needs its read permission). */
    private fun releaseBackupFolder(uri: String) {
        if (uri == library.folder) return
        try {
            contentResolver.releasePersistableUriPermission(Uri.parse(uri),
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
        } catch (e: Exception) {
        }
    }

    /** After quitting a game: an automatic backup into the folder if one is due (spec §4), off the UI thread. */
    private fun autoBackup() {
        val folder = backupSchedule.folder ?: return
        val now = System.currentTimeMillis()
        if (autoRunning || !backupSchedule.due(now)) return
        autoRunning = true
        val app = appVersion()
        val phone = phoneName()
        thread(name = "auto-backup") {
            val ok = try {
                val f = BackupFolder(contentResolver, Uri.parse(folder))
                val doc = f.create(BackupSchedule.fileName(now)) ?: error("couldn't make the file")
                try {
                    contentResolver.openOutputStream(doc, "w")?.use { backups.backUp(it, app, phone, now) } ?: error("no stream")
                } catch (e: Exception) {
                    f.delete(doc)
                    throw e
                }
                val all = f.list()
                val old = BackupSchedule.toDelete(all.map { it.first }).toSet()
                for ((name, uri) in all) if (name in old) f.delete(uri)
                true
            } catch (e: Exception) {
                false
            }
            runOnUiThread {
                autoRunning = false
                if (ok) {
                    backupSchedule.lastAuto = now
                    backupSchedule.lastBackup = now
                    backupSchedule.save()
                    refreshBackupStatus()
                } else if (!autoFailSaid) {
                    autoFailSaid = true
                    toast("Automatic backup failed. Check the folder in Settings → Backup.")
                }
            }
        }
    }
```

- [x] **Step 5: Build and run all the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: Kotlin 175, 0 failures (`UiTextTest` included: "→" is allowed, no "…" / "&").
Run: `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest` — Expected: on-device 74, 0 failures (`BackupDeviceTest` passes).
Run: `bash tools/dev.sh install && adb shell dumpsys package com.tengen.gbaemu | grep -i "flags=\|ALLOW_BACKUP"`
Expected: the package's flags no longer list `ALLOW_BACKUP`.

- [x] **Step 6: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/BackupFolder.kt app/src/main/res/xml/data_extraction_rules.xml app/src/main/AndroidManifest.xml app/src/main/java/com/tengen/gbaemu/MainActivity.kt app/src/androidTest/java/com/tengen/gbaemu/BackupDeviceTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-11-backup.md
git commit -m "feat: backup and restore in the app, Android's own backup off

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Claude's check, milestone 11 exit

**Files:** docs only (`docs/PROGRESS.md`, this plan, the spec's status, the parent spec's status line and §10 row 11, `CLAUDE.md` key files).

- [x] **Step 1: Full runs**

Run: `bash tools/dev.sh test` — Expected: CTest 120/120.
Run: `bash tools/dev.sh jvmtest` — Expected: Kotlin 175, 0 failures.
Run: `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest` — Expected: on-device 74, 0 failures.

- [x] **Step 2: Back up the emulator app's data first** (adb, as in 10b's check): `files/library.json`, `files/saves/`, `files/states/`, `files/cheats/`, `files/layout.json` (if any), `files/skins/`, `files/boxart/boxart.json`, `shared_prefs/*.xml` → the scratchpad, with `adb exec-out run-as com.tengen.gbaemu cat <file> > <copy>` (folders file by file after `run-as … ls -R`).

- [x] **Step 3: Claude's check on the emulator** (UI with `adb shell input` / uiautomator dumps, screenshots to look at)

1. Settings shows the **Backup** row ("No backup yet"); the Backup page as in the spec.
2. **Back up now** → the save-as picker with `GBA Emulator backup <date> <time>.zip` → save to Download → "Backup saved (… MB)."; the row says "Last backup today …". `adb pull` the zip: its entries are the spec §3 paths only (no BIOS, no `library.json`).
3. Change things: delete a slot state (load page), switch Smooth on, remap a key, add a cheat to hack 1, remove an imported skin (if one) or switch skins.
4. **Restore from backup** → pick the zip → the question with the right counts → **Restore** → "Restored."; the slot is back, Smooth is off again, the key and the cheat are as before, the skin is back; hack 1 resumes from its Continue state.
5. **Undo restore** → the step-3 changes are back; the Undo button is gone.
6. Restore the zip again (back to the backup's state).
7. A random `.zip` (e.g. a `.deltaskin` renamed, or `tests/` zip) and a text file renamed `.zip` → "That file isn't a backup this app can restore."; nothing changed.
8. **Automatic backup** on → pick a new folder `Download/GBA backups` (with a file `notes.txt` pushed into it first) → quit a game → a backup appears there (`adb shell ls "/sdcard/Download/GBA backups"`); quit again → no new one (24 h). To move time: force-stop the app, write a `backup-state.json` with `"lastAuto": 0` (and the same `folder`) on the PC, `adb push` it to `/data/local/tmp/`, `adb shell chmod 644 /data/local/tmp/backup-state.json`, `adb shell run-as com.tengen.gbaemu cp /data/local/tmp/backup-state.json files/backup-state.json`, open the app, quit a game. Repeat until 6 automatic backups were made → 5 kept, `notes.txt` untouched. (Each run needs a different minute in its name — wait a minute between runs, or delete the newest by hand.) Automatic off → no more.
9. In a game, Settings → Backup shows "Quit the game to back up or restore." with the buttons greyed out.
10. `adb shell dumpsys package com.tengen.gbaemu` → no `ALLOW_BACKUP` flag.

- [x] **Step 4: Put the emulator back** — the Step 2 copies back (checked identical), Automatic backup off, the test folder and zips deleted. Note anything else that changed in PROGRESS.md.

- [x] **Step 5: Docs, tag, merge question**

Update: the spec's status → implemented; the parent spec's status line and §10 row 11 → done; `CLAUDE.md` key files (current spec / plan → none, last finished = 11's); `PROGRESS.md` (Completed, Project status, Next, Notes: `files/backup-state.json`, `files/restore/`); every box of this plan ticked. Commit, then `git tag milestone-11` and ask the user whether to merge into `main` (superpowers:finishing-a-development-branch).
