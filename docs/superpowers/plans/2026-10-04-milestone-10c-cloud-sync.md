# Milestone 10c — Cloud Sync (Google Drive + Dropbox) — Implementation Plan

> **CANCELLED by the user 2026-10-04 before Task 1** (local saves only — users back up their saves themselves, without the app's help; fewer integrations and app keys helps licensing and third-party audits when the app goes public). Not executed; kept as a record.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** a game's save and save states follow the user between phones through Google Drive (hidden app folder) or Dropbox (app folder): leaving a game uploads, opening a game downloads newer first, the game list checks every game, and a clash asks the user.

**Architecture:** Pure Kotlin does all the deciding and is tested on the PC: `SyncReceipt` (the receipt), `SyncFiles` (a game's files on the phone, swapped in whole with a backup, the agreed receipts, a lock for the game being played), `GameSync` (nothing / upload / download / clash, receipt uploaded last, downloads checked by SHA-256), `SyncWorker` (one job at a time), `CloudPrefs`, and two `CloudDrive`s over plain HTTPS — `GoogleDrive` (REST v3) and `DropboxDrive` (HTTP API) — with `DropboxAuth` (PKCE). Android code adds Google's sign-in (`GoogleAuth`, Play services), the Cloud sync page, the spinner and the clash pop-up, and the wiring in `MainActivity`. The emulator core and the native code are not touched.

**Tech Stack:** Kotlin, Jetpack Compose (Material 3 1.4.0, material-icons-extended 1.7.8), `HttpURLConnection`, `MessageDigest`, `java.util.Base64`, Play services `play-services-auth` 22.0.0 (`AuthorizationClient`), JUnit 4, Compose UI test 1.11.4, Gradle `--offline`, Git Bash `tools/dev.sh`.

**Spec:** `docs/superpowers/specs/2026-10-04-milestone-10c-cloud-sync-design.md`

---

## Conventions

- Git Bash from the repo root, on the branch `milestone-10c`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push>` (the Pixel 9 phone emulator `Pixel_9_API_36_1`, a Google APIs image; the user's USB phones only in Task 13 and only after asking). Gradle runs `--offline`; the one download (`play-services-auth`, Task 1) uses the new `bash tools/dev.sh online` only after the user's OK. Ask before restarting or cold-booting the emulator.
- The core and the native code are not touched: CTest stays 120/120 (run once in Task 13).
- Counting tests: Kotlin unit tests `ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'` (total, failures); on-device `grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml` after `rm -rf app/build/outputs/androidTest-results`.
- Kotlin: 152 → 156 (Task 2) → 161 (Task 3) → 167 (Task 4) → 183 (Task 5) → 189 (Task 6) → 195 (Task 7) → 196 (Task 8) → 202 (Task 10) → 208 (Task 11). On-device: 70 → 73 (Task 8).
- Test-first: each task's tests are written first and fail (they don't compile without the new code), then the code.
- One commit per task; messages end with the Co-Authored-By trailer. Each commit also includes `docs/PROGRESS.md` (the task's Completed entry, the 10c task table, Project status, Next, Last updated) and this plan (ticked boxes).
- **Stops (CLAUDE.md):** Task 1 (the user's one-time setup and the download OK) and Task 13 (sign-ins on the emulator and phone, installing on the phone, the user's check). Every other task runs straight on. Claude never types a password and never taps Google's or Dropbox's Allow without the user's OK in chat (granting access is the user's).
- Every string in `src/main` must pass `UiTextTest` (no "…", "...", "&" in a string literal). URLs and form bodies with `&` are built with `Net.form`, which joins with the **Char** `'&'`.
- Unit tests never touch the internet: `Net` is tested against a local server, the drives and `DropboxAuth` with a fake `send`, `GameSync` with `FakeDrive`.

## File map

```
tools/dev.sh (online), app/build.gradle.kts, gradle.properties (dropboxKey)                                      Task 1
app/src/main/java/com/tengen/gbaemu/Net.kt; app/src/test/.../NetTest.kt                                          Task 2
app/src/main/java/com/tengen/gbaemu/SyncReceipt.kt (new); app/src/test/.../SyncReceiptTest.kt (new)               Task 3
app/src/main/java/com/tengen/gbaemu/CloudDrive.kt (new), SyncFiles.kt (new);
  app/src/test/.../FakeDrive.kt (new), SyncFilesTest.kt (new)                                                    Task 4
app/src/main/java/com/tengen/gbaemu/GameSync.kt (new); app/src/test/.../GameSyncTest.kt (new)                     Task 5
app/src/main/java/com/tengen/gbaemu/SyncWorker.kt (new), CloudPrefs.kt (new);
  app/src/test/.../SyncWorkerTest.kt (new), CloudPrefsTest.kt (new)                                              Task 6
app/src/main/java/com/tengen/gbaemu/GoogleDrive.kt (new); app/src/test/.../GoogleDriveTest.kt (new)               Task 7
app/src/main/java/com/tengen/gbaemu/Settings.kt, SettingsScreens.kt, Screens.kt;
  app/src/test/.../SettingsTest.kt; app/src/androidTest/.../ScreensTest.kt                                       Task 8
app/src/main/java/com/tengen/gbaemu/GoogleAuth.kt (new), MainActivity.kt                                         Task 9
app/src/main/java/com/tengen/gbaemu/DropboxDrive.kt (new); app/src/test/.../DropboxDriveTest.kt (new)             Task 10
app/src/main/java/com/tengen/gbaemu/DropboxAuth.kt (new); app/src/test/.../DropboxAuthTest.kt (new)               Task 11
app/build.gradle.kts, app/src/main/AndroidManifest.xml, MainActivity.kt                                          Task 12
(Claude's check on the emulator + phone A, the user's check, docs, tag)                                   Task 13
```

(`app/src/test/...` = `app/src/test/java/com/tengen/gbaemu/`; `app/src/androidTest/...` = `app/src/androidTest/java/com/tengen/gbaemu/`.)

---

### Task 1: The one-time setup (the user, Claude guiding) and the library

**Files:**
- Modify: `tools/dev.sh` (the `online` command), `app/build.gradle.kts` (the dependency), `gradle.properties` (`dropboxKey`)

This task stops for the user (CLAUDE.md: they must do something). Ask everything in one message, then guide step by step.

- [ ] **Step 1: Read the debug signing key's SHA-1** (the app on every phone is built and signed on this PC)

Run: `"/c/Program Files/Android/Android Studio/jbr/bin/keytool.exe" -list -v -keystore ~/.android/debug.keystore -alias androiddebugkey -storepass android | grep SHA1`
Expected: one line `SHA1: AA:BB:…` (20 bytes). Note it for Step 2.

- [ ] **Step 2: Ask the user (one message) and guide them**

1. **OK to download** `com.google.android.gms:play-services-auth:22.0.0` and what it needs (Google's Maven, a few MB) — the build is `--offline`.
2. **Google Cloud Console** (console.cloud.google.com, signed in with their Google account):
   1. New project, e.g. "GBA Emulator".
   2. APIs and services → Library → **Google Drive API** → Enable.
   3. Google Auth Platform (OAuth consent screen) → Get started: app name "GBA Emulator", their email as support and contact, Audience **External**; Data access → Add scope `https://www.googleapis.com/auth/drive.appdata` (".../auth/drive.appdata", non-sensitive); Audience → **Publish app** (In production — no review is needed for non-sensitive scopes; this avoids "Testing" mode's 7-day sign-in limit).
   4. Clients → Create client → **Android**: package name `com.tengen.gbaemu`, SHA-1 fingerprint from Step 1.
3. **On the phone emulator:** Settings → Passwords, passkeys and accounts → Add account → Google, signed in by the user (Claude never types it).
4. **Dropbox** (dropbox.com/developers/apps, signed in with their Dropbox account; a free account is enough):
   1. Create app → **Scoped access** → **App folder** → name, e.g. "GBA Emulator Sync".
   2. Permissions tab: tick `files.metadata.read`, `files.content.read`, `files.content.write` → Submit.
   3. Settings tab: OAuth 2 → Redirect URIs → add `db-<the app key>://auth` (the app key is shown on the same page) → Add. **If the page refuses the link, stop and tell Claude** (spec §8: then the Dropbox sign-in is decided with the user).
   4. Tell Claude the **App key** (not the secret — it isn't needed).

Wait for the user to say it's done.

- [ ] **Step 3: Add `online` to `tools/dev.sh`**

In `tools/dev.sh`, after the line `    jvmtest) gradle_run :app:testDebugUnitTest ;;` add:

```bash
    online) (cd "$ROOT" && JAVA_HOME="$(cygpath -m "$JBR")" ./gradlew -PgbaForceGeneric="$GENERIC_FLAG" "${@:-:app:assembleDebug}") ;; # Gradle WITH the internet: only to download a new library, after the user's OK
```

- [ ] **Step 4: Add the library and the Dropbox key**

In `app/build.gradle.kts`, in `dependencies { … }` after the `material-icons-extended-android` line:

```kotlin
    implementation("com.google.android.gms:play-services-auth:22.0.0") // Google Drive sign-in (10c)
```

At the end of `gradle.properties` (the app key from Step 2; with PKCE it is not a secret):

```properties
# Dropbox app key for cloud sync (10c); public with PKCE.
dropboxKey=<the app key the user gave>
```

- [ ] **Step 5: Download once, then build offline**

Run: `bash tools/dev.sh online` then `bash tools/dev.sh apk`
Expected: both `BUILD SUCCESSFUL`; the second proves the library is now in the Gradle cache. Then `bash tools/dev.sh jvmtest` → Kotlin 152, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add tools/dev.sh app/build.gradle.kts gradle.properties docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md
git commit -m "build: Google sign-in library and the Dropbox key for cloud sync

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Any request (`Net.send`) and form bodies (`Net.form`)

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Net.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/NetTest.kt` (+4)

- [ ] **Step 1: Write the failing tests**

In `NetTest.kt`, the server must read a request's body (else the client may see a reset). Replace the `Server` class's `init` block's request reading:

```kotlin
                    s.use {
                        val reader = s.getInputStream().bufferedReader()
                        val head = generateSequence { reader.readLine() }.takeWhile { it.isNotEmpty() }.toList()
                        lastRequest = head.joinToString("\n")
                        val length = head.firstNotNullOfOrNull { it.substringAfter("Content-Length: ", "").toIntOrNull() } ?: 0
                        val body = CharArray(length)
                        var n = 0
                        while (n < length) {
                            val r = reader.read(body, n, length - n)
                            if (r < 0) break
                            n += r
                        }
                        lastBody = String(body, 0, n)
```

(the rest of the block — `val path = …` onwards — unchanged) and add the field under `lastRequest`:

```kotlin
        @Volatile var lastBody = ""
```

Add the tests at the end of the class:

```kotlin
    @Test
    fun sendPostsTheBodyAndHeadersAndReadsTheReply() {
        Server(mapOf("/up" to (200 to "ok".toByteArray()))).use { s ->
            val r = Net.send("POST", "${s.url}/up", mapOf("X-Test" to "1", "Content-Type" to "text/plain"), "abc".toByteArray())
            assertEquals(200, r.status)
            assertEquals("ok", r.text)
            assertTrue(s.lastRequest, s.lastRequest.startsWith("POST /up "))
            assertTrue(s.lastRequest, "X-Test: 1" in s.lastRequest)
            assertEquals("abc", s.lastBody)
        }
    }

    @Test
    fun sendReadsAnErrorsBody() {
        Server(mapOf("/bad" to (409 to "{\"error_summary\": \"path/not_found/\"}".toByteArray()))).use { s ->
            val r = Net.send("POST", "${s.url}/bad")
            assertEquals(409, r.status)
            assertTrue(r.text, "not_found" in r.text)
        }
    }

    @Test
    fun sendWithNothingListeningHasStatusZero() {
        val port = ServerSocket(0).use { it.localPort }
        val r = Net.send("GET", "http://127.0.0.1:$port/x")
        assertEquals(0, r.status)
        assertEquals(null, r.body)
    }

    @Test
    fun formEncodesEachPart() {
        assertEquals("a=1&b=x%20y%2Fz&c=%3D%26", Net.form("a" to "1", "b" to "x y/z", "c" to "=&"))
    }
```

- [ ] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh jvmtest`
Expected: compile errors (`send`, `form`, `Reply` unknown).

- [ ] **Step 3: Write the code**

In `Net.kt` add the import `import java.net.URLEncoder` and, inside `object Net` after `get`:

```kotlin
    /** A reply to [send]: the HTTP status (0: no reply at all) and its body (null: unreadable, or more than allowed). */
    class Reply(val status: Int, val body: ByteArray?) {
        val text: String get() = body?.toString(Charsets.UTF_8) ?: ""
    }

    /**
     * Any request (10c): [method] with [headers] and an optional [body] (15 s to connect and to read). The body of an
     * error reply is read too (Google and Dropbox explain errors there). More than [max] bytes back: no body.
     */
    fun send(method: String, url: String, headers: Map<String, String> = emptyMap(), body: ByteArray? = null,
             max: Int = 1 shl 20): Reply = try {
        val c = URL(url).openConnection() as HttpURLConnection
        try {
            c.connectTimeout = TIMEOUT
            c.readTimeout = TIMEOUT
            c.requestMethod = method
            c.setRequestProperty("User-Agent", USER_AGENT)
            for ((k, v) in headers) c.setRequestProperty(k, v)
            if (body != null) {
                c.doOutput = true
                c.setFixedLengthStreamingMode(body.size)
                c.outputStream.use { it.write(body) }
            }
            val status = c.responseCode
            val stream = if (status >= 400) c.errorStream else c.inputStream
            Reply(status, if (stream == null) ByteArray(0) else stream.use { readUpTo(it, max) })
        } finally {
            c.disconnect()
        }
    } catch (e: Exception) {
        Reply(0, null)
    }

    /** Name-value pairs as a query or a form body, `a=1&b=x%20y` (each part URL-encoded; spaces as %20). */
    fun form(vararg pairs: Pair<String, String>): String =
        pairs.joinToString('&'.toString()) { (k, v) -> encode(k) + "=" + encode(v) }

    private fun encode(s: String): String = URLEncoder.encode(s, "UTF-8").replace("+", "%20")
```

- [ ] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest`
Expected: PASS; Kotlin 156, 0 failures (`UiTextTest` passes: `'&'` is a Char).

- [ ] **Step 5: Commit** (with PROGRESS.md and this plan)

```bash
git add app/src/main/java/com/tengen/gbaemu/Net.kt app/src/test/java/com/tengen/gbaemu/NetTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md
git commit -m "feat: any web request and form bodies (Net.send, Net.form)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The receipt (`SyncReceipt`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/SyncReceipt.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/SyncReceiptTest.kt` (new, 5 tests)

- [ ] **Step 1: Write the failing test**

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SyncReceiptTest {
    private val h1 = "a".repeat(64)
    private val h2 = "b".repeat(64)
    private val receipt = SyncReceipt("e86b3224", "phone A", "p-1", 1_759_600_000_123L, 7,
        mapOf("save.sav" to h1, "slot10.state" to h2, "auto.png" to h1))

    @Test
    fun writtenAndReadBackTheSame() {
        val text = receipt.toJson()
        assertTrue(text, text.startsWith("{\"version\": 1, "))
        assertEquals(receipt, SyncReceipt.parse(text))
    }

    @Test
    fun theSameUploadIsTheSamePhoneAndNumber() {
        assertTrue(receipt.sameUpload(receipt.copy(time = 5, files = emptyMap())))
        assertFalse(receipt.sameUpload(receipt.copy(count = 8)))
        assertFalse(receipt.sameUpload(receipt.copy(phoneId = "p-2")))
        assertFalse(receipt.sameUpload(null))
    }

    @Test
    fun aNewerVersionOrDamagedTextIsNull() {
        assertNull(SyncReceipt.parse(receipt.toJson().replace("\"version\": 1", "\"version\": 2")))
        assertNull(SyncReceipt.parse("{"))
        assertNull(SyncReceipt.parse("{\"version\": 1, \"game\": \"e86b3224\"}"))
        assertNull(SyncReceipt.parse(receipt.toJson() + "x"))
    }

    @Test
    fun aFileNameHashOrGameThatIsNotOursIsRefused() {
        assertNull(SyncReceipt.parse(receipt.copy(files = mapOf("../x" to h1)).toJson()))
        assertNull(SyncReceipt.parse(receipt.copy(files = mapOf("slot11.state" to h1)).toJson()))
        assertNull(SyncReceipt.parse(receipt.copy(files = mapOf("save.sav" to "abc")).toJson()))
        assertNull(SyncReceipt.parse(receipt.copy(game = "../e86b").toJson()))
    }

    @Test
    fun sha256IsTheKnownValue() {
        assertEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", SyncReceipt.sha256("abc".toByteArray()))
    }
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile error (`SyncReceipt` unknown).

- [ ] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/SyncReceipt.kt`:

```kotlin
package com.tengen.gbaemu

import java.security.MessageDigest

/**
 * A game's receipt in the cloud (10c, spec §3): which file is which (`save.sav`, `auto.state`, `slot1.png`, …: the
 * SHA-256 of its bytes, which is also its name in the cloud), the phone that uploaded it, when, and the upload's
 * number. Uploaded last, so the cloud's set is always whole; [phoneId] + [count] name one upload.
 */
data class SyncReceipt(val game: String, val phone: String, val phoneId: String, val time: Long, val count: Long,
                       val files: Map<String, String>) {
    fun sameUpload(other: SyncReceipt?): Boolean = other != null && other.phoneId == phoneId && other.count == count

    fun toJson(): String = buildString {
        append("{\"version\": ").append(VERSION)
        append(", \"game\": ").append(Json.str(game))
        append(", \"phone\": ").append(Json.str(phone))
        append(", \"phoneId\": ").append(Json.str(phoneId))
        append(", \"time\": ").append(time)
        append(", \"count\": ").append(count)
        append(", \"files\": {")
        files.entries.sortedBy { it.key }.forEachIndexed { i, (name, hash) ->
            if (i > 0) append(", ")
            append(Json.str(name)).append(": ").append(Json.str(hash))
        }
        append("}}")
    }

    companion object {
        const val VERSION = 1

        /** The names a receipt may use: the save and the states' files (a cloud file can't name anything else). */
        val NAME = Regex("save\\.sav|(auto|quick|slot([1-9]|10))\\.(state|png)")
        private val HASH = Regex("[0-9a-f]{64}")
        private val GAME = Regex("[0-9a-f]{8}")

        /** null: damaged, another version, or naming something that isn't a game's file. */
        fun parse(text: String): SyncReceipt? = try {
            val r = Json.Reader(text)
            val m = r.value() as Map<*, *>
            r.end()
            val files = (m["files"] as Map<*, *>).entries.associate { (k, v) -> k as String to v as String }
            val receipt = SyncReceipt(m["game"] as String, m["phone"] as String, m["phoneId"] as String,
                (m["time"] as Double).toLong(), (m["count"] as Double).toLong(), files)
            val ok = (m["version"] as Double) == VERSION.toDouble() && GAME.matches(receipt.game) &&
                files.all { (k, v) -> NAME.matches(k) && HASH.matches(v) }
            if (ok) receipt else null
        } catch (e: Exception) {
            null
        }

        fun sha256(bytes: ByteArray): String =
            MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    }
}
```

- [ ] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 161, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/SyncReceipt.kt app/src/test/java/com/tengen/gbaemu/SyncReceiptTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md
git commit -m "feat: the cloud sync receipt (SyncReceipt)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The cloud's shape (`CloudDrive`), a fake one, and the phone's side (`SyncFiles`, `GameLock`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/CloudDrive.kt`, `app/src/main/java/com/tengen/gbaemu/SyncFiles.kt`
- Create (test helper): `app/src/test/java/com/tengen/gbaemu/FakeDrive.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/SyncFilesTest.kt` (new, 6 tests)

- [ ] **Step 1: Write the fake drive and the failing test**

Create `app/src/test/java/com/tengen/gbaemu/FakeDrive.kt`:

```kotlin
package com.tengen.gbaemu

/** A [CloudDrive] in memory for the tests, with switches for the ways a real one fails. */
class FakeDrive : CloudDrive {
    val files = LinkedHashMap<String, ByteArray>()
    val uploads = mutableListOf<String>()
    val deletes = mutableListOf<String>()
    var lists = 0
    var problem: CloudDrive.Kind? = null // every call throws it
    var uploadsLeft = Int.MAX_VALUE // then uploads fail (FAILED), like a lost connection half-way
    var corrupt: String? = null // this path downloads with its first byte changed

    private fun check() {
        problem?.let { throw CloudDrive.Problem(it) }
    }

    override fun list(prefix: String): List<CloudDrive.Entry> {
        check()
        lists++
        return files.keys.filter { it.startsWith(prefix) }.map { CloudDrive.Entry(it, "ref:$it") }
    }

    override fun download(entry: CloudDrive.Entry): ByteArray? {
        check()
        val bytes = files[entry.path] ?: return null
        return if (entry.path == corrupt) bytes.copyOf().also { it[0] = (it[0] + 1).toByte() } else bytes.copyOf()
    }

    override fun upload(path: String, bytes: ByteArray) {
        check()
        if (uploadsLeft <= 0) throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
        uploadsLeft--
        uploads += path
        files[path] = bytes.copyOf()
    }

    override fun delete(entry: CloudDrive.Entry) {
        check()
        deletes += entry.path
        files.remove(entry.path)
    }
}
```

Create `app/src/test/java/com/tengen/gbaemu/SyncFilesTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SyncFilesTest {
    private val id = "e86b3224"
    private val dir = Files.createTempDirectory("sync").toFile()
    private val files = SyncFiles(dir)

    private fun write(name: String, text: String) = files.file(id, name).apply { parentFile?.mkdirs(); writeText(text) }
    private fun texts(map: Map<String, ByteArray>) = map.mapValues { String(it.value) }

    @Test
    fun readFindsTheSaveAndTheStatesByReceiptName() {
        write("save.sav", "s")
        write("auto.state", "a")
        write("slot3.png", "p").setLastModified(5_000_000L)
        File(dir, "states/$id/other.txt").writeText("x")
        File(dir, "states/$id/auto.state.old").writeText("x")
        File(dir, "saves/$id.sav.bak").writeText("x")
        assertEquals(mapOf("auto.state" to "a", "save.sav" to "s", "slot3.png" to "p"), texts(files.read(id)))
        assertEquals(File(dir, "saves/$id.sav"), files.file(id, "save.sav"))
        assertEquals(File(dir, "states/$id/slot3.png"), files.file(id, "slot3.png"))
        assertTrue(files.newest(id) >= 5_000_000L)
        assertEquals(0L, files.newest("00000000"))
    }

    @Test
    fun replaceSwapsTheSetAndKeepsTheOldOneAsABackup() {
        write("save.sav", "old save")
        write("slot1.state", "old slot")
        assertTrue(files.replace(id, mapOf("save.sav" to "new".toByteArray(), "auto.state" to "auto".toByteArray()), 100))
        assertEquals(mapOf("auto.state" to "auto", "save.sav" to "new"), texts(files.read(id)))
        val backup = files.backups(id).single()
        assertEquals("100", backup.name)
        assertEquals("old save", File(backup, "save.sav").readText())
        assertEquals("old slot", File(backup, "slot1.state").readText())
        assertFalse(File(dir, "sync/tmp/$id").exists())
    }

    @Test
    fun onlyTheNewestThreeBackupsAreKept() {
        for (t in 1L..4L) {
            write("save.sav", "v$t")
            files.replace(id, mapOf("save.sav" to "n$t".toByteArray()), t)
        }
        assertEquals(listOf("2", "3", "4"), files.backups(id).map { it.name })
    }

    @Test
    fun aGameBeingPlayedIsNotTouched() {
        write("save.sav", "mine")
        files.lock.hold(id)
        assertFalse(files.replace(id, mapOf("save.sav" to "theirs".toByteArray()), 1))
        assertNull(files.lock.unlessHeld(id) { 1 })
        assertEquals("mine", files.file(id, "save.sav").readText())
        files.lock.release(id)
        assertTrue(files.replace(id, mapOf("save.sav" to "theirs".toByteArray()), 2))
        assertEquals("theirs", files.file(id, "save.sav").readText())
    }

    @Test
    fun theAgreedReceiptIsKeptAndForgotten() {
        val r = SyncReceipt(id, "Pixel", "p", 1, 1, mapOf("save.sav" to "c".repeat(64)))
        assertNull(files.agreed(id))
        files.agree(id, r)
        assertEquals(r, files.agreed(id))
        File(dir, "sync/cloud.json").writeText("{}")
        files.forgetAll()
        assertNull(files.agreed(id))
        assertTrue(File(dir, "sync/cloud.json").exists())
    }

    @Test
    fun keepAsideWritesABackupWithoutTouchingTheGame() {
        write("save.sav", "mine")
        files.keepAside(id, mapOf("save.sav" to "theirs".toByteArray()), 7)
        assertEquals("mine", files.file(id, "save.sav").readText())
        assertEquals("theirs", File(files.backups(id).single(), "save.sav").readText())
    }
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile errors (`CloudDrive`, `SyncFiles` unknown).

- [ ] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/CloudDrive.kt`:

```kotlin
package com.tengen.gbaemu

/** One web request, as [Net.send] makes it (the drives get it passed in, so tests can answer for the internet). */
typealias Send = (method: String, url: String, headers: Map<String, String>, body: ByteArray?, max: Int) -> Net.Reply

/**
 * Where the receipts and files live (10c): Google Drive's hidden app folder or Dropbox's app folder. Paths are
 * `<game id>/receipt.json` and `<game id>/<sha256>`. Every call may throw [Problem].
 */
interface CloudDrive {
    /** One file: its path, and what the service needs to fetch or delete it (Drive's file id, Dropbox's path). */
    data class Entry(val path: String, val ref: String)

    enum class Kind { FAILED, SIGNED_OUT, FULL }

    /** The service couldn't be reached ([Kind.FAILED]), wants the user to sign in again, or is full. */
    class Problem(val kind: Kind) : Exception(kind.name)

    /** Every file whose path starts with [prefix] ("" = all); of two with one path, the newer. */
    fun list(prefix: String): List<Entry>

    /** The file's bytes; null if it is gone. */
    fun download(entry: Entry): ByteArray?

    /** Writes [bytes] at [path], replacing what was there. */
    fun upload(path: String, bytes: ByteArray)

    /** Removes the file (already gone is fine). */
    fun delete(entry: Entry)
}
```

Create `app/src/main/java/com/tengen/gbaemu/SyncFiles.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File

/**
 * One phone's side of cloud sync (10c, spec §4–5): a game's save and states by their receipt names, swapped in whole
 * with the old set kept in `files/sync-backup/<id>/<time>/` (the newest [BACKUPS] per game), and the receipt this
 * phone last agreed with the cloud (`files/sync/<id>.json`). [lock]: a game being played is never touched.
 */
class SyncFiles(private val filesDir: File, val lock: GameLock = GameLock()) {
    private val syncDir = File(filesDir, "sync")

    /** Where the file with receipt name [name] lives for game [id]. */
    fun file(id: String, name: String): File =
        if (name == SAVE) SaveFiles.file(filesDir, id) else File(States(filesDir, id).dir, name)

    /** The receipt names of the game's files now (other files in its states folder are left out). */
    private fun names(id: String): List<String> {
        val states = States(filesDir, id).dir.listFiles()?.filter { it.isFile && it.name != SAVE && SyncReceipt.NAME.matches(it.name) }
        return listOfNotNull(SAVE.takeIf { SaveFiles.file(filesDir, id).isFile }) + states.orEmpty().map { it.name }
    }

    /** The game's files now, by receipt name. */
    fun read(id: String): Map<String, ByteArray> = names(id).associateWith { file(id, it).readBytes() }.toSortedMap()

    /** When the newest of the game's files was written (0: none). */
    fun newest(id: String): Long = names(id).maxOfOrNull { file(id, it).lastModified() } ?: 0L

    /**
     * [files] become the game's set: written to `sync/tmp/<id>` first, then the current set moves to a new backup
     * folder and the new files move in (names not in [files] are gone from the game, kept in the backup). False: the
     * game is being played (nothing changed).
     */
    fun replace(id: String, files: Map<String, ByteArray>, now: Long): Boolean = lock.unlessHeld(id) {
        val tmp = File(syncDir, "tmp/$id").apply { deleteRecursively(); mkdirs() }
        for ((name, bytes) in files) File(tmp, name).writeBytes(bytes)
        val backup = backupDir(id, now)
        for (name in names(id)) move(file(id, name), File(backup, name))
        for (name in files.keys) move(File(tmp, name), file(id, name))
        tmp.delete()
        trimBackups(id)
        true
    } ?: false

    /** Keeps [files] (the set the user didn't pick) as a backup of game [id]. */
    fun keepAside(id: String, files: Map<String, ByteArray>, now: Long) {
        val dir = backupDir(id, now)
        for ((name, bytes) in files) File(dir, name).writeBytes(bytes)
        trimBackups(id)
    }

    /** The game's backup folders, oldest first. */
    fun backups(id: String): List<File> = File(filesDir, "sync-backup/$id").listFiles()
        ?.filter { it.isDirectory }?.sortedBy { it.name.toLongOrNull() ?: 0L }.orEmpty()

    fun agreed(id: String): SyncReceipt? = File(syncDir, "$id.json").takeIf { it.isFile }?.let { SyncReceipt.parse(it.readText()) }

    fun agree(id: String, receipt: SyncReceipt) {
        syncDir.mkdirs()
        val f = File(syncDir, "$id.json")
        val tmp = File(syncDir, "$id.json.tmp")
        tmp.writeText(receipt.toJson())
        if (!tmp.renameTo(f)) {
            f.delete()
            tmp.renameTo(f)
        }
    }

    /** Disconnect: every agreed receipt forgotten, so connecting again is a first connect. */
    fun forgetAll() {
        syncDir.listFiles()?.filter { AGREED.matches(it.name) }?.forEach { it.delete() }
    }

    private fun backupDir(id: String, now: Long): File {
        var t = now
        while (File(filesDir, "sync-backup/$id/$t").exists()) t++
        return File(filesDir, "sync-backup/$id/$t").apply { mkdirs() }
    }

    private fun trimBackups(id: String) = backups(id).dropLast(BACKUPS).forEach { it.deleteRecursively() }

    private fun move(from: File, to: File) {
        to.parentFile?.mkdirs()
        if (!from.renameTo(to)) {
            from.copyTo(to, overwrite = true)
            from.delete()
        }
    }

    companion object {
        const val SAVE = "save.sav"
        const val BACKUPS = 3
        private val AGREED = Regex("[0-9a-f]{8}\\.json")
    }
}

/** The games being played (10c): [unlessHeld] runs only for other games, and [hold] waits until one running finishes. */
class GameLock {
    private val held = HashSet<String>()

    @Synchronized fun hold(id: String) {
        held += id
    }

    @Synchronized fun release(id: String) {
        held -= id
    }

    @Synchronized fun <T> unlessHeld(id: String, block: () -> T): T? = if (id in held) null else block()
}
```

- [ ] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 167, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/CloudDrive.kt app/src/main/java/com/tengen/gbaemu/SyncFiles.kt app/src/test/java/com/tengen/gbaemu/FakeDrive.kt app/src/test/java/com/tengen/gbaemu/SyncFilesTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md
git commit -m "feat: the cloud's shape and the phone's side of sync (CloudDrive, SyncFiles)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Syncing one game (`GameSync`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/GameSync.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/GameSyncTest.kt` (new, 16 tests)

- [ ] **Step 1: Write the failing test**

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/** Two phones, A and B, on one [FakeDrive]: what each sync does (spec §5). */
class GameSyncTest {
    private val id = "e86b3224"
    private val id2 = "1f1c08fb"
    private val drive = FakeDrive()
    private var clock = 1000L

    private inner class Phone(val name: String) {
        val dir: File = Files.createTempDirectory(name).toFile()
        val files = SyncFiles(dir)
        val sync = GameSync(drive, files, name, "$name-id") { clock++ }
        fun write(name: String, text: String, game: String = id) = files.file(game, name).apply { parentFile?.mkdirs(); writeText(text) }
        fun read(game: String = id) = files.read(game).mapValues { String(it.value) }
    }

    private val a = Phone("A")
    private val b = Phone("B")
    private fun receipt() = SyncReceipt.parse(String(drive.files.getValue("$id/receipt.json")))!!
    private fun problem(kind: CloudDrive.Kind, block: () -> Unit) {
        try {
            block()
            fail("expected $kind")
        } catch (p: CloudDrive.Problem) {
            assertEquals(kind, p.kind)
        }
    }

    @Test
    fun theFirstUploadPutsTheFilesThenTheReceipt() {
        a.write("save.sav", "s1")
        a.write("auto.state", "a1")
        assertEquals(GameSync.Result.Uploaded, a.sync.sync(id))
        assertEquals("$id/receipt.json", drive.uploads.last())
        assertEquals(3, drive.files.size)
        val r = receipt()
        assertEquals(1L, r.count)
        assertEquals("A", r.phone)
        assertEquals(r, a.files.agreed(id))
    }

    @Test
    fun nothingChangedIsTheSame() {
        a.write("save.sav", "s1")
        a.sync.sync(id)
        val uploads = drive.uploads.size
        assertEquals(GameSync.Result.Same, a.sync.sync(id))
        assertEquals(uploads, drive.uploads.size)
        assertEquals(GameSync.Result.Same, b.sync.sync("00000000")) // nothing anywhere
    }

    @Test
    fun theOtherPhoneGetsTheWholeSet() {
        a.write("save.sav", "s1")
        a.write("slot2.state", "x")
        a.write("slot2.png", "p")
        a.sync.sync(id)
        assertEquals(GameSync.Result.Downloaded, b.sync.sync(id))
        assertEquals(a.read(), b.read())
        assertEquals(receipt(), b.files.agreed(id))
    }

    @Test
    fun aChangeOnOnePhoneTravelsAndTheOldSetIsKept() {
        a.write("save.sav", "s1")
        a.sync.sync(id)
        b.sync.sync(id)
        b.write("save.sav", "s2")
        assertEquals(GameSync.Result.Uploaded, b.sync.sync(id))
        assertEquals(2L, receipt().count)
        assertEquals(GameSync.Result.Downloaded, a.sync.sync(id))
        assertEquals(mapOf("save.sav" to "s2"), a.read())
        assertEquals("s1", File(a.files.backups(id).single(), "save.sav").readText())
    }

    @Test
    fun bothChangedIsAClashWithTheCloudsPicture() {
        a.write("save.sav", "s1")
        a.sync.sync(id)
        b.sync.sync(id)
        a.write("save.sav", "a2")
        b.write("save.sav", "b2")
        b.write("auto.png", "b-picture")
        b.sync.sync(id)
        val r = a.sync.sync(id)
        assertTrue(r.toString(), r is GameSync.Result.Clash)
        r as GameSync.Result.Clash
        assertEquals("B", r.cloud.phone)
        assertArrayEquals("b-picture".toByteArray(), r.picture)
        assertEquals(mapOf("save.sav" to "a2"), a.read()) // nothing changed yet
    }

    @Test
    fun pickingTheCloudBringsItDownAndKeepsThisPhonesAsABackup() {
        bothChangedIsAClashWithTheCloudsPicture()
        assertEquals(GameSync.Result.Downloaded, a.sync.resolve(id, keepThisPhone = false))
        assertEquals(b.read(), a.read())
        assertEquals("a2", File(a.files.backups(id).last(), "save.sav").readText())
        assertEquals(GameSync.Result.Same, a.sync.sync(id))
    }

    @Test
    fun pickingThisPhoneUploadsItAndKeepsTheCloudsAsABackup() {
        bothChangedIsAClashWithTheCloudsPicture()
        assertEquals(GameSync.Result.Uploaded, a.sync.resolve(id, keepThisPhone = true))
        assertEquals("b2", File(a.files.backups(id).last(), "save.sav").readText())
        assertEquals("b-picture", File(a.files.backups(id).last(), "auto.png").readText())
        assertEquals(GameSync.Result.Downloaded, b.sync.sync(id))
        assertEquals(mapOf("save.sav" to "a2"), b.read())
    }

    @Test
    fun aFirstConnectWithTheSameFilesAgreesAndWithOthersClashes() {
        a.write("save.sav", "s1")
        a.sync.sync(id)
        b.write("save.sav", "s1")
        assertEquals(GameSync.Result.Same, b.sync.sync(id))
        assertEquals(receipt(), b.files.agreed(id))
        val c = Phone("C")
        c.write("save.sav", "other")
        assertTrue(c.sync.sync(id) is GameSync.Result.Clash)
    }

    @Test
    fun anUploadStoppedHalfWayLeavesTheOldSetWhole() {
        a.write("save.sav", "s1")
        a.write("auto.state", "a1")
        a.sync.sync(id)
        b.sync.sync(id)
        a.write("save.sav", "s2")
        a.write("auto.state", "a2")
        drive.uploadsLeft = 1
        problem(CloudDrive.Kind.FAILED) { a.sync.sync(id) }
        assertEquals(1L, receipt().count)
        assertEquals(GameSync.Result.Same, b.sync.sync(id))
        assertEquals(mapOf("auto.state" to "a1", "save.sav" to "s1"), b.read())
        drive.uploadsLeft = Int.MAX_VALUE
        assertEquals(GameSync.Result.Uploaded, a.sync.sync(id))
        assertEquals(GameSync.Result.Downloaded, b.sync.sync(id))
        assertEquals(mapOf("auto.state" to "a2", "save.sav" to "s2"), b.read())
    }

    @Test
    fun aBrokenDownloadChangesNothing() {
        a.write("save.sav", "s1")
        a.sync.sync(id)
        drive.corrupt = "$id/" + SyncReceipt.sha256("s1".toByteArray())
        b.write("save.sav", "mine")
        b.files.agree(id, receipt().copy(phoneId = "old", files = mapOf("save.sav" to SyncReceipt.sha256("mine".toByteArray()))))
        assertEquals(GameSync.Result.Skipped, b.sync.sync(id))
        assertEquals(mapOf("save.sav" to "mine"), b.read())
        assertEquals("old", b.files.agreed(id)!!.phoneId)
        assertTrue(b.files.backups(id).isEmpty())
    }

    @Test
    fun tidyingKeepsThePreviousSetAndDeletesOlderOnes() {
        for (v in 1..3) {
            a.write("save.sav", "v$v")
            a.sync.sync(id)
        }
        fun stored(text: String) = "$id/" + SyncReceipt.sha256(text.toByteArray()) in drive.files
        assertFalse(stored("v1"))
        assertTrue(stored("v2"))
        assertTrue(stored("v3"))
    }

    @Test
    fun problemsChangeNothingOnThePhone() {
        a.write("save.sav", "s1")
        a.sync.sync(id)
        drive.problem = CloudDrive.Kind.SIGNED_OUT
        problem(CloudDrive.Kind.SIGNED_OUT) { b.sync.sync(id) }
        drive.problem = CloudDrive.Kind.FULL
        problem(CloudDrive.Kind.FULL) { b.sync.sync(id) }
        assertTrue(b.read().isEmpty())
        assertNull(b.files.agreed(id))
    }

    @Test
    fun aReceiptOfAnotherVersionIsLeftAlone() {
        drive.files["$id/receipt.json"] = "{\"version\": 2}".toByteArray()
        a.write("save.sav", "s1")
        assertEquals(GameSync.Result.Skipped, a.sync.sync(id))
        assertTrue(drive.uploads.isEmpty())
    }

    @Test
    fun aPlayedGameIsLeftAloneAndAPausedOneOnlyUploads() {
        a.write("save.sav", "s1")
        a.sync.sync(id)
        b.sync.sync(id)
        a.write("save.sav", "s2")
        a.sync.sync(id)
        b.files.lock.hold(id)
        assertEquals(GameSync.Result.Skipped, b.sync.sync(id)) // being played: not even read
        assertEquals(GameSync.Result.Skipped, b.sync.sync(id, snapshot = b.files.read(id))) // paused: no download
        assertEquals(mapOf("save.sav" to "s1"), b.read())
        b.files.lock.release(id)
        b.sync.sync(id)
        b.files.lock.hold(id)
        b.write("save.sav", "s3")
        assertEquals(GameSync.Result.Uploaded, b.sync.sync(id, snapshot = b.files.read(id)))
        assertEquals("B", receipt().phone)
    }

    @Test
    fun bothChangedTheSameWayIsTheSame() {
        a.write("save.sav", "s1")
        a.sync.sync(id)
        b.sync.sync(id)
        a.write("save.sav", "x")
        b.write("save.sav", "x")
        a.sync.sync(id)
        assertEquals(GameSync.Result.Same, b.sync.sync(id))
        assertEquals(receipt(), b.files.agreed(id))
    }

    @Test
    fun syncAllUsesOneListingForEveryGame() {
        a.write("save.sav", "s1")
        a.write("save.sav", "t1", game = id2)
        a.sync.sync(id)
        a.sync.sync(id2)
        val lists = drive.lists
        val results = b.sync.syncAll(listOf(id, id2))
        assertEquals(mapOf(id to GameSync.Result.Downloaded, id2 to GameSync.Result.Downloaded), results)
        assertEquals(lists + 1, drive.lists)
        assertEquals(mapOf("save.sav" to "t1"), b.read(id2))
    }
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile error (`GameSync` unknown).

- [ ] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/GameSync.kt`:

```kotlin
package com.tengen.gbaemu

/**
 * Cloud sync for one game (10c, spec §5): this phone's files, the receipt it last agreed with and the cloud's
 * receipt decide between nothing, upload, download and a clash. Files go up before the receipt; a download is checked
 * against its hashes before anything on the phone changes. Calls to [drive] may throw [CloudDrive.Problem].
 */
class GameSync(private val drive: CloudDrive, private val files: SyncFiles, private val phone: String,
               private val phoneId: String, private val clock: () -> Long = System::currentTimeMillis) {
    sealed interface Result {
        data object Same : Result
        data object Uploaded : Result
        data object Downloaded : Result

        /** Both sides changed: the user picks ([resolve]). [cloud]: its receipt; [picture]: its Continue picture. */
        class Clash(val cloud: SyncReceipt, val picture: ByteArray?) : Result

        /** Left for later: the game is being played, the cloud's receipt can't be used, or a file didn't arrive whole. */
        data object Skipped : Result
    }

    private enum class Step { SAME, UPLOAD, DOWNLOAD, CLASH }

    /**
     * Syncs game [id]. [entries]: the cloud's listing for it, if already known. [snapshot]: the files of a game that
     * is paused, not quit (Home) — then only an upload is done.
     */
    fun sync(id: String, entries: List<CloudDrive.Entry>? = null, snapshot: Map<String, ByteArray>? = null): Result {
        val listed = entries ?: drive.list("$id/")
        val (usable, cloud) = cloudReceipt(id, listed)
        if (!usable) return Result.Skipped
        val local = snapshot ?: files.lock.unlessHeld(id) { files.read(id) } ?: return Result.Skipped
        val hashes = local.mapValues { SyncReceipt.sha256(it.value) }
        val agreed = files.agreed(id)
        val localChanged = if (agreed == null) local.isNotEmpty() else hashes != agreed.files
        val cloudChanged = cloud != null && !cloud.sameUpload(agreed)
        val step = when {
            cloud == null -> if (local.isEmpty()) Step.SAME else Step.UPLOAD
            cloud.files == hashes -> {
                files.agree(id, cloud)
                Step.SAME
            }
            agreed == null -> if (local.isEmpty()) Step.DOWNLOAD else Step.CLASH
            cloudChanged && localChanged -> Step.CLASH
            cloudChanged -> Step.DOWNLOAD
            localChanged -> Step.UPLOAD
            else -> Step.SAME
        }
        if (snapshot != null && step != Step.UPLOAD && step != Step.SAME) return Result.Skipped
        return when (step) {
            Step.SAME -> Result.Same
            Step.UPLOAD -> upload(id, local, hashes, cloud, listed)
            Step.DOWNLOAD -> download(id, cloud!!, listed)
            Step.CLASH -> Result.Clash(cloud!!, picture(id, cloud, listed))
        }
    }

    /** Every game in [ids], with one listing of the cloud; the first problem stops it (thrown). */
    fun syncAll(ids: Collection<String>): Map<String, Result> {
        val all = drive.list("").groupBy { it.path.substringBefore('/') }
        return ids.associateWith { sync(it, all[it].orEmpty()) }
    }

    /** After a clash: [keepThisPhone] uploads this phone's set (the cloud's kept aside first), else the cloud's comes down. */
    fun resolve(id: String, keepThisPhone: Boolean): Result {
        val listed = drive.list("$id/")
        val (usable, cloud) = cloudReceipt(id, listed)
        if (!usable) return Result.Skipped
        if (!keepThisPhone) return if (cloud == null) Result.Same else download(id, cloud, listed)
        val local = files.lock.unlessHeld(id) { files.read(id) } ?: return Result.Skipped
        if (cloud != null) files.keepAside(id, fetch(id, cloud, listed) ?: return Result.Skipped, clock())
        return upload(id, local, local.mapValues { SyncReceipt.sha256(it.value) }, cloud, listed)
    }

    /** The cloud's receipt for [id]: (true, null) none; (false, null) one that can't be used (damaged, newer, gone). */
    private fun cloudReceipt(id: String, listed: List<CloudDrive.Entry>): Pair<Boolean, SyncReceipt?> {
        val entry = listed.find { it.path == "$id/$RECEIPT" } ?: return true to null
        val receipt = drive.download(entry)?.let { SyncReceipt.parse(it.toString(Charsets.UTF_8)) }?.takeIf { it.game == id }
        return (receipt != null) to receipt
    }

    private fun upload(id: String, local: Map<String, ByteArray>, hashes: Map<String, String>, base: SyncReceipt?,
                       listed: List<CloudDrive.Entry>): Result {
        val have = listed.mapTo(HashSet()) { it.path }
        for ((name, bytes) in local) {
            val path = "$id/${hashes.getValue(name)}"
            if (have.add(path)) drive.upload(path, bytes)
        }
        val now = drive.list("$id/")
        val (usable, current) = cloudReceipt(id, now)
        val unchanged = if (current == null) base == null else current.sameUpload(base)
        if (!usable || !unchanged) return Result.Skipped // another phone uploaded meanwhile: next time
        val receipt = SyncReceipt(id, phone, phoneId, clock(), (base?.count ?: 0) + 1, hashes)
        drive.upload("$id/$RECEIPT", receipt.toJson().toByteArray())
        files.agree(id, receipt)
        val keep = receipt.files.values.toSet() + base?.files?.values.orEmpty()
        try {
            for (e in now) {
                val name = e.path.removePrefix("$id/")
                if (name != RECEIPT && name !in keep) drive.delete(e)
            }
        } catch (p: CloudDrive.Problem) { // tidying is for later too
        }
        return Result.Uploaded
    }

    private fun download(id: String, cloud: SyncReceipt, listed: List<CloudDrive.Entry>): Result {
        val got = fetch(id, cloud, listed) ?: return Result.Skipped
        if (!files.replace(id, got, clock())) return Result.Skipped
        files.agree(id, cloud)
        return Result.Downloaded
    }

    /** The cloud's set, each file checked against its hash (this phone's copies reused); null if one didn't arrive whole. */
    private fun fetch(id: String, cloud: SyncReceipt, listed: List<CloudDrive.Entry>): Map<String, ByteArray>? {
        val byPath = listed.associateBy { it.path }
        val mine = (files.lock.unlessHeld(id) { files.read(id) } ?: emptyMap()).values.associateBy { SyncReceipt.sha256(it) }
        val got = sortedMapOf<String, ByteArray>()
        for ((name, hash) in cloud.files) {
            val bytes = mine[hash] ?: byPath["$id/$hash"]?.let(drive::download) ?: return null
            if (SyncReceipt.sha256(bytes) != hash) return null
            got[name] = bytes
        }
        return got
    }

    /** The cloud's Continue picture for the clash pop-up (none if it can't be had). */
    private fun picture(id: String, cloud: SyncReceipt, listed: List<CloudDrive.Entry>): ByteArray? {
        val entry = cloud.files["auto.png"]?.let { h -> listed.find { it.path == "$id/$h" } } ?: return null
        return try {
            drive.download(entry)
        } catch (p: CloudDrive.Problem) {
            null
        }
    }

    companion object {
        const val RECEIPT = "receipt.json"
    }
}
```

- [ ] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 183, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/GameSync.kt app/src/test/java/com/tengen/gbaemu/GameSyncTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md
git commit -m "feat: syncing one game with the cloud (GameSync)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: One job at a time (`SyncWorker`) and what's remembered (`CloudPrefs`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/SyncWorker.kt`, `app/src/main/java/com/tengen/gbaemu/CloudPrefs.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/SyncWorkerTest.kt` (new, 4), `app/src/test/java/com/tengen/gbaemu/CloudPrefsTest.kt` (new, 2)

- [ ] **Step 1: Write the failing tests**

Create `SyncWorkerTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.nio.file.Files
import java.util.Collections
import java.util.concurrent.CountDownLatch
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class SyncWorkerTest {
    private val id = "e86b3224"
    private val drive = FakeDrive()
    private val files = SyncFiles(Files.createTempDirectory("worker").toFile())
    private val sync = GameSync(drive, files, "A", "A-id")
    private val done = Collections.synchronizedList(mutableListOf<SyncWorker.Done>())

    init {
        files.file(id, "save.sav").apply { parentFile?.mkdirs(); writeText("s") }
    }

    @Test
    fun aGameJobReportsItsResultAndJobsRunInOrder() {
        val w = SyncWorker { sync }
        val order = Collections.synchronizedList(mutableListOf<String>())
        w.game(id) { done += it; order += "game" }
        w.then { order += "then" }
        w.waitIdle()
        assertEquals(listOf("game", "then"), order)
        assertEquals(mapOf(id to GameSync.Result.Uploaded), done.single().results)
    }

    @Test
    fun aProblemIsReportedNotThrown() {
        drive.problem = CloudDrive.Kind.FULL
        val w = SyncWorker { sync }
        w.game(id) { done += it }
        w.waitIdle()
        assertEquals(CloudDrive.Kind.FULL, done.single().problem)
        assertTrue(done.single().results.isEmpty())
    }

    @Test
    fun syncIsOffWhenThereIsNoGameSync() {
        val w = SyncWorker { null }
        w.all(listOf(id)) { done += it }
        w.waitIdle()
        assertTrue(done.single().results.isEmpty())
        assertEquals(null, done.single().problem)
        w.all(listOf(id)) { done += it } // queued again: the first one has run
        w.waitIdle()
        assertEquals(2, done.size)
    }

    @Test
    fun anAllJobIsQueuedOnceUntilItStarts() {
        val gate = CountDownLatch(1)
        val w = SyncWorker { gate.await(); sync }
        w.game(id) { done += it }
        w.all(listOf(id)) { done += it }
        w.all(listOf(id)) { done += it }
        gate.countDown()
        w.waitIdle()
        assertEquals(2, done.size)
    }
}
```

Create `CloudPrefsTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CloudPrefsTest {
    private val file = File(Files.createTempDirectory("prefs").toFile(), "sync/cloud.json")

    @Test
    fun thePhoneIdIsMadeOnceAndKept() {
        val id = CloudPrefs(file).phoneId
        assertTrue(id.length >= 16)
        assertEquals(id, CloudPrefs(file).phoneId)
        assertTrue(id != CloudPrefs(File(file.parentFile, "other.json")).phoneId)
    }

    @Test
    fun theLastSyncAndStoppedAreSavedAndADamagedFileStartsAfresh() {
        val p = CloudPrefs(file)
        assertEquals(0L, p.lastSync)
        assertFalse(p.stopped)
        p.lastSync = 1_759_600_000_123L
        p.stopped = true
        p.save()
        val q = CloudPrefs(file)
        assertEquals(1_759_600_000_123L, q.lastSync)
        assertTrue(q.stopped)
        file.writeText("{")
        assertEquals(0L, CloudPrefs(file).lastSync)
    }
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile errors (`SyncWorker`, `CloudPrefs` unknown).

- [ ] **Step 3: Write the code**

Create `SyncWorker.kt`:

```kotlin
package com.tengen.gbaemu

import java.util.concurrent.Executors

/**
 * Runs cloud sync off the UI thread, one job at a time (10c, spec §6), so a game's upload on leaving is done before
 * opening it again downloads. [make] makes the [GameSync] for the chosen service (null: sync is off or stopped);
 * it is called on the worker's thread, as are the jobs' callbacks.
 */
class SyncWorker(private val make: () -> GameSync?) {
    private val queue = Executors.newSingleThreadExecutor { r -> Thread(r, "cloud-sync").apply { isDaemon = true } }
    @Volatile private var allQueued = false

    /** What a job did: per game, or the problem that stopped it (then no results). */
    class Done(val results: Map<String, GameSync.Result>, val problem: CloudDrive.Kind?)

    /** Every game in [ids] (the list shows); not queued again while one waits. */
    fun all(ids: Collection<String>, onDone: (Done) -> Unit) {
        if (allQueued) return
        allQueued = true
        run(onDone, start = { allQueued = false }) { it.syncAll(ids) }
    }

    /** One game (opening it, leaving it; [snapshot] for a paused game: upload only). */
    fun game(id: String, snapshot: Map<String, ByteArray>? = null, onDone: (Done) -> Unit) =
        run(onDone) { mapOf(id to it.sync(id, snapshot = snapshot)) }

    /** The user's pick after a clash. */
    fun resolve(id: String, keepThisPhone: Boolean, onDone: (Done) -> Unit) =
        run(onDone) { mapOf(id to it.resolve(id, keepThisPhone)) }

    /** [block] after every job queued so far (e.g. forgetting the agreed receipts on Disconnect). */
    fun then(block: () -> Unit) {
        queue.execute(block)
    }

    /** Waits until every job queued so far has finished (tests). */
    fun waitIdle() {
        queue.submit {}.get()
    }

    private fun run(onDone: (Done) -> Unit, start: () -> Unit = {}, job: (GameSync) -> Map<String, GameSync.Result>) {
        queue.execute {
            start()
            val done = try {
                make()?.let { Done(job(it), null) } ?: Done(emptyMap(), null)
            } catch (p: CloudDrive.Problem) {
                Done(emptyMap(), p.kind)
            } catch (e: Exception) {
                Done(emptyMap(), CloudDrive.Kind.FAILED)
            }
            onDone(done)
        }
    }
}
```

Create `CloudPrefs.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.util.UUID

/**
 * What cloud sync remembers on this phone (10c), in `files/sync/cloud.json`: this install's random [phoneId] (made
 * once), when it last synced, and whether it [stopped] because the service wants the user to sign in again.
 */
class CloudPrefs(private val file: File) {
    var phoneId = ""
        private set
    @Volatile var lastSync = 0L
    @Volatile var stopped = false

    init {
        try {
            val m = Json.Reader(file.readText()).value() as Map<*, *>
            phoneId = m["phoneId"] as String
            lastSync = (m["lastSync"] as Double).toLong()
            stopped = m["stopped"] as Boolean
        } catch (e: Exception) { // none yet, or damaged: start afresh
        }
        if (phoneId.isEmpty()) {
            phoneId = UUID.randomUUID().toString()
            save()
        }
    }

    @Synchronized fun save() {
        file.parentFile?.mkdirs()
        file.writeText("{\"phoneId\": ${Json.str(phoneId)}, \"lastSync\": $lastSync, \"stopped\": $stopped}")
    }
}
```

- [ ] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 189, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/SyncWorker.kt app/src/main/java/com/tengen/gbaemu/CloudPrefs.kt app/src/test/java/com/tengen/gbaemu/SyncWorkerTest.kt app/src/test/java/com/tengen/gbaemu/CloudPrefsTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md
git commit -m "feat: the sync worker and what cloud sync remembers

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Google Drive (`GoogleDrive`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/GoogleDrive.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/GoogleDriveTest.kt` (new, 6 tests)

- [ ] **Step 1: Write the failing test**

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class GoogleDriveTest {
    /** One request as the drive made it. */
    class Asked(val method: String, val url: String, val headers: Map<String, String>, val body: ByteArray?)

    private val asked = mutableListOf<Asked>()
    private val replies = ArrayDeque<Net.Reply>()
    private val send: Send = { method, url, headers, body, _ ->
        asked += Asked(method, url, headers, body)
        replies.removeFirstOrNull() ?: Net.Reply(500, ByteArray(0))
    }
    private val tokens = mutableListOf<Boolean>()
    private var token: String? = "t1"
    private val drive = GoogleDrive({ fresh -> tokens += fresh; if (fresh && token != null) "t2" else token }, send)

    private fun reply(status: Int, text: String = "") = replies.add(Net.Reply(status, text.toByteArray()))
    private fun problem(kind: CloudDrive.Kind, block: () -> Unit) {
        try {
            block()
            fail("expected $kind")
        } catch (p: CloudDrive.Problem) {
            assertEquals(kind, p.kind)
        }
    }

    @Test
    fun listAsksForTheAppFolderWithTheTokenAndKeepsTheNewestOfEachName() {
        reply(200, """{"files": [
            {"id": "1", "name": "e86b3224/receipt.json", "modifiedTime": "2026-10-04T10:00:00.000Z"},
            {"id": "2", "name": "e86b3224/receipt.json", "modifiedTime": "2026-10-04T11:00:00.000Z"},
            {"id": "3", "name": "1f1c08fb/receipt.json", "modifiedTime": "2026-10-04T09:00:00.000Z"}]}""")
        assertEquals(listOf(CloudDrive.Entry("e86b3224/receipt.json", "2")), drive.list("e86b3224/"))
        val a = asked.single()
        assertEquals("GET", a.method)
        assertTrue(a.url, a.url.startsWith(GoogleDrive.FILES + "?"))
        assertTrue(a.url, "spaces=appDataFolder" in a.url)
        assertEquals("Bearer t1", a.headers["Authorization"])
    }

    @Test
    fun listFollowsEveryPage() {
        reply(200, """{"files": [{"id": "1", "name": "a/x", "modifiedTime": "1"}], "nextPageToken": "p2"}""")
        reply(200, """{"files": [{"id": "2", "name": "a/y", "modifiedTime": "1"}]}""")
        assertEquals(listOf("a/x", "a/y"), drive.list("").map { it.path })
        assertTrue(asked[1].url, "pageToken=p2" in asked[1].url)
    }

    @Test
    fun uploadMakesANewFileThenDeletesTheOlderOnes() {
        reply(200, """{"id": "new"}""")
        reply(200, """{"files": [{"id": "new", "name": "e86b3224/receipt.json", "modifiedTime": "2"},
            {"id": "old", "name": "e86b3224/receipt.json", "modifiedTime": "1"}]}""")
        reply(204)
        drive.upload("e86b3224/receipt.json", "RECEIPT".toByteArray())
        assertEquals(listOf("POST", "GET", "DELETE"), asked.map { it.method })
        assertTrue(asked[0].url, asked[0].url.startsWith(GoogleDrive.UPLOAD + "?uploadType=multipart"))
        assertTrue(asked[0].headers["Content-Type"]!!.startsWith("multipart/related; boundary="))
        val body = String(asked[0].body!!)
        assertTrue(body, "{\"name\": \"e86b3224/receipt.json\", \"parents\": [\"appDataFolder\"]}" in body)
        assertTrue(body, "RECEIPT" in body)
        assertTrue(asked[1].url, "name%20%3D%20%27e86b3224%2Freceipt.json%27" in asked[1].url)
        assertEquals(GoogleDrive.FILES + "/old", asked[2].url)
    }

    @Test
    fun downloadReadsTheBytesAndAMissingFileIsNull() {
        reply(200, "bytes")
        assertArrayEquals("bytes".toByteArray(), drive.download(CloudDrive.Entry("a/x", "id7")))
        assertEquals(GoogleDrive.FILES + "/id7?alt=media", asked[0].url)
        reply(404, "{}")
        assertNull(drive.download(CloudDrive.Entry("a/x", "id7")))
    }

    @Test
    fun aRefusedTokenIsRenewedOnceThenItIsSignedOut() {
        reply(401)
        reply(200, """{"files": []}""")
        assertTrue(drive.list("").isEmpty())
        assertEquals(listOf(false, true), tokens)
        assertEquals("Bearer t2", asked[1].headers["Authorization"])
        reply(401)
        reply(401)
        problem(CloudDrive.Kind.SIGNED_OUT) { drive.list("") }
        token = null
        problem(CloudDrive.Kind.SIGNED_OUT) { drive.list("") }
    }

    @Test
    fun aFullDriveOrAFailureThrowTheirKind() {
        reply(403, """{"error": {"errors": [{"reason": "storageQuotaExceeded"}]}}""")
        problem(CloudDrive.Kind.FULL) { drive.upload("a/x", ByteArray(1)) }
        reply(403, """{"error": {"errors": [{"reason": "insufficientPermissions"}]}}""")
        problem(CloudDrive.Kind.SIGNED_OUT) { drive.list("") }
        reply(500)
        problem(CloudDrive.Kind.FAILED) { drive.list("") }
        replies.add(Net.Reply(0, null))
        problem(CloudDrive.Kind.FAILED) { drive.list("") }
        reply(200, "not json")
        problem(CloudDrive.Kind.FAILED) { drive.list("") }
    }
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile error (`GoogleDrive` unknown).

- [ ] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/GoogleDrive.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.ByteArrayOutputStream

/**
 * [CloudDrive] in Google Drive's hidden app folder (10c; REST v3, scope `drive.appdata`): a path is a file's name.
 * Uploading makes a new file and then deletes older ones of that name, so a reader always finds a whole file.
 * [token] gives an access token (true: a fresh one, the last was refused); null: the user must sign in again.
 */
class GoogleDrive(private val token: (fresh: Boolean) -> String?, private val send: Send) : CloudDrive {
    private class File(val id: String, val name: String, val modified: String)

    override fun list(prefix: String): List<CloudDrive.Entry> = files(null).filter { it.name.startsWith(prefix) }
        .groupBy { it.name }.map { (_, same) -> same.maxBy { it.modified } }.map { CloudDrive.Entry(it.name, it.id) }

    override fun download(entry: CloudDrive.Entry): ByteArray? {
        val r = call("GET", "$FILES/${entry.ref}?alt=media", null, null, MAX_FILE)
        return if (r.status == 404) null else r.body
    }

    override fun upload(path: String, bytes: ByteArray) {
        val boundary = "gba-emulator-" + SyncReceipt.sha256(bytes).take(24)
        val meta = "{\"name\": ${Json.str(path)}, \"parents\": [\"appDataFolder\"]}"
        val body = ByteArrayOutputStream().apply {
            write(("--$boundary\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n$meta\r\n" +
                "--$boundary\r\nContent-Type: application/octet-stream\r\n\r\n").toByteArray())
            write(bytes)
            write("\r\n--$boundary--\r\n".toByteArray())
        }.toByteArray()
        val r = call("POST", "$UPLOAD?" + Net.form("uploadType" to "multipart", "fields" to "id"),
            "multipart/related; boundary=$boundary", body)
        if (r.status == 404) throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
        val id = read(r)["id"] as? String ?: throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
        for (f in files(path)) if (f.id != id) delete(CloudDrive.Entry(path, f.id))
    }

    override fun delete(entry: CloudDrive.Entry) {
        call("DELETE", "$FILES/${entry.ref}", null, null)
    }

    /** Every file in the app folder ([name]: only those with that name), page by page. */
    private fun files(name: String?): List<File> {
        val out = mutableListOf<File>()
        var page: String? = null
        do {
            val q = mutableListOf("spaces" to "appDataFolder", "pageSize" to "1000", "fields" to "nextPageToken,files(id,name,modifiedTime)")
            if (name != null) q += "q" to "name = '${name.replace("'", "\\'")}'"
            page?.let { q += "pageToken" to it }
            val m = read(call("GET", "$FILES?" + Net.form(*q.toTypedArray()), null, null))
            try {
                for (f in m["files"] as List<*>) {
                    f as Map<*, *>
                    out += File(f["id"] as String, f["name"] as String, f["modifiedTime"] as String)
                }
            } catch (e: Exception) {
                throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
            }
            page = m["nextPageToken"] as? String
        } while (page != null)
        return out
    }

    private fun read(r: Net.Reply): Map<*, *> = try {
        Json.Reader(r.text).run { (value() as Map<*, *>).also { end() } }
    } catch (e: Exception) {
        throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
    }

    /** One request with the token; a refused token is renewed once. 2xx and 404 come back; the rest throw. */
    private fun call(method: String, url: String, type: String?, body: ByteArray?, max: Int = 1 shl 20): Net.Reply {
        var fresh = false
        while (true) {
            val t = token(fresh) ?: throw CloudDrive.Problem(CloudDrive.Kind.SIGNED_OUT)
            val headers = HashMap<String, String>()
            headers["Authorization"] = "Bearer $t"
            if (type != null) headers["Content-Type"] = type
            val r = send(method, url, headers, body, max)
            when {
                r.status == 401 && !fresh -> fresh = true
                r.status == 401 -> throw CloudDrive.Problem(CloudDrive.Kind.SIGNED_OUT)
                r.status == 403 && "storageQuotaExceeded" in r.text -> throw CloudDrive.Problem(CloudDrive.Kind.FULL)
                r.status == 403 && ("insufficient" in r.text || "SCOPE" in r.text) -> throw CloudDrive.Problem(CloudDrive.Kind.SIGNED_OUT)
                r.status == 404 -> return r
                r.status in 200..299 && r.body != null -> return r
                else -> throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
            }
        }
    }

    companion object {
        const val FILES = "https://www.googleapis.com/drive/v3/files"
        const val UPLOAD = "https://www.googleapis.com/upload/drive/v3/files"
        const val MAX_FILE = 16 shl 20
    }
}
```

- [ ] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 195, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/GoogleDrive.kt app/src/test/java/com/tengen/gbaemu/GoogleDriveTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md
git commit -m "feat: Google Drive's hidden app folder as the cloud (GoogleDrive)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: The screens — the Cloud sync page, the spinner, the clash pop-up

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Settings.kt`, `SettingsScreens.kt`, `Screens.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/SettingsTest.kt` (3 extended, +1), `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt` (+3)

- [ ] **Step 1: Write the failing tests**

In `SettingsTest.kt`: in `nothingStoredMeansTheDefaultsWhichAreTodaysBehaviour` add `assertEquals(Settings.CLOUD_OFF, s.cloud)`; in `everySettingIsSavedAndReadBack` add `cloud = Settings.CLOUD_DROPBOX` to the `Settings(...)` (after `boxArt = false`); in `aValueThatDoesNotReadIsItsDefault` add `"cloud" to 3` to the list. Add:

```kotlin
    @Test
    fun theCloudSyncRowSaysTheServiceAndItsState() {
        assertEquals("Off", cloudSummary(Settings.CLOUD_OFF, CloudStatus(lastSync = 5)))
        assertEquals("Google Drive · not synced yet", cloudSummary(Settings.CLOUD_GOOGLE, CloudStatus()))
        assertEquals("Dropbox · sign in again", cloudSummary(Settings.CLOUD_DROPBOX, CloudStatus(lastSync = 5, stopped = true)))
        val synced = cloudSummary(Settings.CLOUD_GOOGLE, CloudStatus(lastSync = System.currentTimeMillis()))
        assertTrue(synced, synced.startsWith("Google Drive · synced today "))
    }
```

(add `import org.junit.Assert.assertTrue` if missing).

In `ScreensTest.kt` add the import `import androidx.compose.ui.test.onAllNodesWithText` and the tests:

```kotlin
    @Test
    fun theCloudSyncPageChoosesSignsInAgainAndDisconnects() {
        val chosen = mutableListOf<Int>()
        var again = 0
        compose.setContent {
            AppTheme {
                SettingsScreen(SettingsPage.CLOUD, Settings(cloud = Settings.CLOUD_GOOGLE), KeyBindings(MemoryStore()), 0, "Built-in",
                    onPage = {}, onBack = {}, onChange = {}, onPickKey = { _, _ -> }, onResetKeys = {},
                    onChooseBios = {}, onBuiltInBios = {}, cloud = CloudStatus(stopped = true),
                    onChooseCloud = { chosen += it }, onSignInAgain = { again++ })
            }
        }
        compose.onNodeWithText("Signed out of Google Drive").assertIsDisplayed()
        compose.onNodeWithText("Not synced yet").assertIsDisplayed()
        compose.onNodeWithText("Sign in again").performClick()
        assertEquals(1, again)
        compose.onNodeWithText("Dropbox").performClick()
        compose.onNodeWithText("Disconnect").performScrollTo().performClick()
        assertEquals(listOf(Settings.CLOUD_DROPBOX, Settings.CLOUD_OFF), chosen)
    }

    @Test
    fun theSpinnerOffersPlayingWithoutSyncing() {
        var played = 0
        compose.setContent { AppTheme { SyncingDialog("Google Drive", onPlayWithout = { played++ }) } }
        compose.onNodeWithText("Getting your latest save").assertIsDisplayed()
        compose.onNodeWithText("From Google Drive").assertIsDisplayed()
        compose.onNodeWithText("Play without syncing").performClick()
        assertEquals(1, played)
    }

    @Test
    fun theClashPopUpShowsBothSidesAndPicks() {
        val picks = mutableListOf<Boolean>()
        var cancelled = 0
        val now = System.currentTimeMillis()
        compose.setContent {
            AppTheme {
                ClashDialog("game C", here = ClashSide(null, "Pixel 9", now), cloud = ClashSide(null, "phone A", now - 60_000),
                    onPick = { picks += it }, onCancel = { cancelled++ })
            }
        }
        compose.onNodeWithText("This game was played on two phones").assertIsDisplayed()
        compose.onNodeWithText("Pixel 9").assertIsDisplayed()
        compose.onNodeWithText("phone A").assertIsDisplayed()
        compose.onAllNodesWithText("Use this one").assertCountEquals(2)
        compose.onAllNodesWithText("Use this one")[1].performClick()
        compose.onAllNodesWithText("Use this one")[0].performClick()
        assertEquals(listOf(false, true), picks)
        compose.onNodeWithText("Cancel").performClick()
        assertEquals(1, cancelled)
    }
```

- [ ] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile errors (`cloud`, `CLOUD_*`, `cloudSummary`, `CloudStatus` unknown).

- [ ] **Step 3: Write the code**

`Settings.kt`: add the field after `boxArt`:

```kotlin
    val cloud: Int = CLOUD_OFF, // cloud sync (10c): CLOUD_OFF, CLOUD_GOOGLE or CLOUD_DROPBOX; set only once signed in
```

in `save` after `store.put("box_art", flag(boxArt))`:

```kotlin
        store.put("cloud", cloud)
```

in `load` after `boxArt = bool(store, "box_art", d.boxArt),`:

```kotlin
                cloud = choice(store, "cloud", CLOUD_OFF..CLOUD_DROPBOX, d.cloud),
```

and in the companion, after `LOOK_STRONG`:

```kotlin
        const val CLOUD_OFF = 0
        const val CLOUD_GOOGLE = 1
        const val CLOUD_DROPBOX = 2
```

and after `LOOK_NAMES`:

```kotlin
        val CLOUD_NAMES = listOf("Off", "Google Drive", "Dropbox")
```

`SettingsScreens.kt`: add the import `import androidx.compose.material.icons.filled.Cloud`; the page `CLOUD("Cloud sync")` in `SettingsPage` (after `SKIN("Skin")`, so `SKIN("Skin"), CLOUD("Cloud sync");`); above `SettingsScreen` add:

```kotlin
/** What the Cloud sync page shows (10c): the last sync (0 = not yet) and whether the user must sign in again. */
data class CloudStatus(val lastSync: Long = 0, val stopped: Boolean = false)

/** The Cloud sync row's summary: "Off", "Google Drive · synced today 14:02", "Dropbox · sign in again". */
fun cloudSummary(service: Int, status: CloudStatus): String {
    if (service == Settings.CLOUD_OFF) return "Off"
    val name = Settings.CLOUD_NAMES[service]
    return when {
        status.stopped -> "$name · sign in again"
        status.lastSync > 0 -> "$name · synced " + playedText(status.lastSync).replaceFirstChar { it.lowercase() }
        else -> "$name · not synced yet"
    }
}
```

`SettingsScreen`'s parameters: after `onRemoveSkin: (Int) -> Unit = {}` add `, cloud: CloudStatus = CloudStatus(), onChooseCloud: (Int) -> Unit = {}, onSignInAgain: () -> Unit = {}`. On the MAIN page, after the BIOS `NavRow`:

```kotlin
                        NavRow(Icons.Filled.Cloud, "Cloud sync", cloudSummary(s.cloud, cloud)) { onPage(SettingsPage.CLOUD) }
```

and a new page before `SettingsPage.BIOS -> {`:

```kotlin
                    SettingsPage.CLOUD -> {
                        ChoiceRow("Sync with", Settings.CLOUD_NAMES, s.cloud, onChooseCloud,
                            help = "Your saves and save states, the same on every phone signed in to the same account. Both phones need the same game file.")
                        if (s.cloud != Settings.CLOUD_OFF) {
                            val name = Settings.CLOUD_NAMES[s.cloud]
                            Text(if (cloud.stopped) "Signed out of $name" else "Connected to $name",
                                style = MaterialTheme.typography.bodyLarge, modifier = Modifier.padding(top = 12.dp))
                            Text(if (cloud.lastSync > 0) "Last synced " + playedText(cloud.lastSync).replaceFirstChar { it.lowercase() } else "Not synced yet",
                                color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(bottom = 12.dp))
                            if (cloud.stopped) {
                                Button(onClick = onSignInAgain, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("Sign in again") }
                                Spacer(Modifier.height(8.dp))
                            }
                            OutlinedButton(onClick = { onChooseCloud(Settings.CLOUD_OFF) }, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                                Text("Disconnect")
                            }
                            Help("Disconnecting keeps everything on this phone and in $name.")
                        }
                    }
```

`SettingsPage.up` needs no change (CLOUD → MAIN by `else`).

`Screens.kt`: after `RestartDialog` add:

```kotlin
/** Opening a game while its newest save comes from the cloud (10c); it opens by itself after 15 s. */
@Composable
fun SyncingDialog(service: String, onPlayWithout: () -> Unit) {
    AlertDialog(
        onDismissRequest = {},
        title = { Text("Getting your latest save") },
        text = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(Modifier.size(24.dp))
                Spacer(Modifier.width(12.dp))
                Text("From $service")
            }
        },
        confirmButton = { TextButton(onClick = onPlayWithout) { Text("Play without syncing") } },
    )
}

/** One side of a clash (10c): its Continue picture, the phone's name and when it was saved. */
class ClashSide(val picture: Bitmap?, val phone: String, val time: Long)

/** A game changed on two phones (10c): the user picks one to play on; the other is kept as a backup. */
@Composable
fun ClashDialog(game: String, here: ClashSide, cloud: ClashSide, onPick: (keepThisPhone: Boolean) -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("This game was played on two phones") },
        text = {
            Column {
                Text("$game changed on both. Pick the one to keep playing; the other is kept as a backup.")
                Spacer(Modifier.height(12.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    ClashCard(here, onPick = { onPick(true) }, modifier = Modifier.weight(1f))
                    ClashCard(cloud, onPick = { onPick(false) }, modifier = Modifier.weight(1f))
                }
            }
        },
        confirmButton = {},
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
private fun ClashCard(side: ClashSide, onPick: () -> Unit, modifier: Modifier) {
    Column(modifier) {
        Box(Modifier.fillMaxWidth().aspectRatio(1.5f).clip(MaterialTheme.shapes.small)
            .background(MaterialTheme.colorScheme.surfaceVariant)) {
            side.picture?.let {
                Image(it.asImageBitmap(), contentDescription = null, contentScale = ContentScale.Fit, modifier = Modifier.fillMaxSize())
            }
        }
        Text(side.phone, style = MaterialTheme.typography.titleSmall, maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(top = 6.dp))
        Text(playedText(side.time), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Button(onClick = onPick, modifier = Modifier.fillMaxWidth().padding(top = 6.dp)) { Text("Use this one") }
    }
}
```

- [ ] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 196, 0 failures.
Run: `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest` — Expected: on-device 73, 0 failures. (If `SessionTest.runsAtGbaSpeed` alone fails by a few frames, rerun once — 10b's notes: the emulator short of memory; the core is untouched.)

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Settings.kt app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/test/java/com/tengen/gbaemu/SettingsTest.kt app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md
git commit -m "feat: the Cloud sync page, the spinner and the clash pop-up

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Google's sign-in (`GoogleAuth`) and the wiring (`MainActivity`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/GoogleAuth.kt`
- Modify: `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`

No new unit tests (Play services and the activity); proven by building, the full test runs, and a sign-in + upload on the emulator in Step 5.

- [ ] **Step 1: Create `GoogleAuth.kt`**

```kotlin
package com.tengen.gbaemu

import android.app.Activity
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import com.google.android.gms.auth.GoogleAuthUtil
import com.google.android.gms.auth.api.identity.AuthorizationRequest
import com.google.android.gms.auth.api.identity.Identity
import com.google.android.gms.common.api.Scope
import com.google.android.gms.tasks.Tasks
import java.util.concurrent.TimeUnit

/**
 * Signing in to Google Drive (10c) through Play services: the user allows the app's hidden Drive folder once
 * ([signIn]: Google's own pop-up); after that [token] gets a 1-hour access token without asking.
 */
class GoogleAuth(private val context: Context) {
    private val request = AuthorizationRequest.builder().setRequestedScopes(listOf(Scope(SCOPE))).build()
    @Volatile private var last: String? = null

    /** Asks for access: [onDone] at once if it was allowed before, else [onAsk] with Google's pop-up to show. */
    fun signIn(activity: Activity, onAsk: (PendingIntent) -> Unit, onDone: (Boolean) -> Unit) {
        Identity.getAuthorizationClient(activity).authorize(request)
            .addOnSuccessListener { r ->
                val ask = r.pendingIntent
                if (r.hasResolution() && ask != null) onAsk(ask) else onDone(r.accessToken != null)
            }
            .addOnFailureListener { onDone(false) }
    }

    /** Google's pop-up came back: true if the user allowed it. */
    fun allowedIn(data: Intent?): Boolean = try {
        Identity.getAuthorizationClient(context).getAuthorizationResultFromIntent(data).accessToken != null
    } catch (e: Exception) {
        false
    }

    /** An access token, on a background thread ([fresh]: the last one was refused); null: the user must allow it again. */
    fun token(fresh: Boolean): String? {
        if (fresh) last?.let { t ->
            try {
                GoogleAuthUtil.clearToken(context, t)
            } catch (e: Exception) {
            }
        }
        val r = try {
            Tasks.await(Identity.getAuthorizationClient(context).authorize(request), 30, TimeUnit.SECONDS)
        } catch (e: Exception) {
            throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
        }
        if (r.hasResolution()) return null
        return r.accessToken.also { last = it }
    }

    companion object {
        const val SCOPE = "https://www.googleapis.com/auth/drive.appdata"
    }
}
```

- [ ] **Step 2: Wire it into `MainActivity.kt`**

Imports to add: `import androidx.activity.result.IntentSenderRequest`.

Fields, after `private var onlineCheats …`:

```kotlin
    private lateinit var syncFiles: SyncFiles // cloud sync, this phone's side (10c)
    private lateinit var cloudPrefs: CloudPrefs
    private lateinit var googleAuth: GoogleAuth
    private lateinit var syncWorker: SyncWorker
    @Volatile private var cloudService = Settings.CLOUD_OFF // settings.cloud, for the sync thread
    private var cloudStatus by mutableStateOf(CloudStatus())
    private var syncing by mutableStateOf<Opening?>(null) // a game waiting for its newest save (the spinner)
    private var clash by mutableStateOf<ClashAsk?>(null)
    private var heldId: String? = null // the game being played: sync leaves its files alone
    private var fullSaid = false // "is full" is said once per run

    /** A game read and waiting to open (its sync first). */
    private class Opening(val entry: GameEntry, val bytes: ByteArray, val id: String, val resumeAuto: Boolean)

    /** A clash shown while [opening] waits. */
    private class ClashAsk(val opening: Opening, val here: ClashSide, val cloud: ClashSide)
```

Launcher, after `pickBox`:

```kotlin
    private val googleAsk = registerForActivityResult(ActivityResultContracts.StartIntentSenderForResult()) { r ->
        googleSignedIn(r.resultCode == RESULT_OK && googleAuth.allowedIn(r.data))
    }
```

In `onCreate`, after `boxArt = BoxArt(...)`:

```kotlin
        syncFiles = SyncFiles(filesDir)
        cloudPrefs = CloudPrefs(File(filesDir, "sync/cloud.json"))
        cloudStatus = CloudStatus(cloudPrefs.lastSync, cloudPrefs.stopped)
        googleAuth = GoogleAuth(applicationContext)
        syncWorker = SyncWorker(::newGameSync)
```

and after `settings = Settings.load(settingsStore)`: `cloudService = settings.cloud`.

In `setContent`, after the `onlineCheats?.let { … }` line:

```kotlin
                syncing?.let { o -> SyncingDialog(serviceName(), onPlayWithout = { syncing = null; openNow(o) }) }
                clash?.let { c ->
                    ClashDialog(c.opening.entry.name, c.here, c.cloud, onPick = ::clashPicked, onCancel = { clash = null })
                }
```

The `SettingsScreen(...)` call: after `onRemoveSkin = ::removeSkin` add `, cloud = cloudStatus, onChooseCloud = ::chooseCloud, onSignInAgain = { chooseCloud(settings.cloud) }`.

In `refreshLibrary`, after both `startBoxArt()` calls add `syncAll()`.

Replace `open(...)` with the reading part plus `openNow` (the rest of the old body, now taking the `Opening`):

```kotlin
    /**
     * Opens a game; with its automatic state it resumes the exact moment ([resumeAuto] false: from the title). With
     * cloud sync on ([sync]), the game's newest save and states come first (up to [OPEN_WAIT_MS], or a clash to pick).
     */
    private fun open(entry: GameEntry, resumeAuto: Boolean = true, sync: Boolean = true) {
        stopBoxArt()
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
        val o = Opening(entry, bytes, GameId.of(bytes), resumeAuto)
        if (!sync || settings.cloud == Settings.CLOUD_OFF || cloudPrefs.stopped) return openNow(o)
        waitFor(o)
        syncWorker.game(o.id) { done -> runOnUiThread { openingSynced(o, done) } }
    }

    /** The spinner for [o]; after [OPEN_WAIT_MS] the game opens with this phone's copy. */
    private fun waitFor(o: Opening) {
        syncing = o
        handler.postDelayed({ if (syncing === o) { syncing = null; openNow(o) } }, OPEN_WAIT_MS)
    }

    private fun openingSynced(o: Opening, done: SyncWorker.Done) {
        synced(done)
        if (syncing !== o) return // played without syncing, or the time ran out
        syncing = null
        if (done.problem == CloudDrive.Kind.FAILED) toast("Couldn't reach ${serviceName()}. Playing this phone's save.")
        val r = done.results[o.id]
        if (r is GameSync.Result.Clash) {
            clash = ClashAsk(o,
                ClashSide(States(filesDir, o.id).loadPicture(States.AUTO), phoneName(), syncFiles.newest(o.id)),
                ClashSide(r.picture?.let { BitmapFactory.decodeByteArray(it, 0, it.size) }, r.cloud.phone, r.cloud.time))
        } else openNow(o)
    }

    /** The user picked a side of the clash: it is synced (spinner), then the game opens. */
    private fun clashPicked(keepThisPhone: Boolean) {
        val c = clash ?: return
        clash = null
        val o = c.opening
        waitFor(o)
        syncWorker.resolve(o.id, keepThisPhone) { done ->
            runOnUiThread {
                synced(done)
                if (syncing !== o) return@runOnUiThread
                syncing = null
                if (done.problem != null) toast("Couldn't reach ${serviceName()}. Playing this phone's save.")
                openNow(o)
            }
        }
    }

    private fun openNow(o: Opening) {
        val entry = o.entry
        val bytes = o.bytes
        val gameId = o.id
        syncFiles.lock.hold(gameId) // waits if a sync is swapping its files right now
        heldId = gameId
        val rom = ByteBuffer.allocateDirect(bytes.size).put(bytes)
        rom.flip()
        val saves = File(filesDir, "saves").apply { mkdirs() }
        currentCode = GameNames.code(bytes)
        val bios = if (settings.biosFile) {
            Bios.load(filesDir) ?: null.also { toast("The BIOS file is missing; the built-in BIOS is used.") }
        } else null
        NativeBridge.setBios(bios)
        val own = gameSettings.of(gameId)
        NativeBridge.setGameOverrides(own.saveType, own.clock)
        if (NativeBridge.loadRom(rom, File(saves, "$gameId.sav").path) != 0) {
            syncFiles.lock.release(gameId)
            heldId = null
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
        if (o.resumeAuto && st.used(States.AUTO)) {
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
```

(This is the old `open()`'s second half, moved as it was; only `resumeAuto` → `o.resumeAuto`, the lock lines and the release before "Not a GBA game." are new.)

The "no save yet" pop-up's Cancel stops the core without `stopGame`, so it must let go of the game too — in `setContent`, `ImportDialog(…, onCancel = { askImport = false; NativeBridge.stop() })` becomes:

```kotlin
                        onCancel = {
                            askImport = false
                            NativeBridge.stop()
                            heldId?.let { syncFiles.lock.release(it) }
                            heldId = null
                        })
```

`stopGame()`: at its end add:

```kotlin
        heldId?.let { syncFiles.lock.release(it) }
        heldId = null
```

`quitGame()`: capture the id first and sync after:

```kotlin
    private fun quitGame() {
        val id = heldId
        saveAuto()
        stopGame()
        if (id != null && cloudOn()) syncWorker.game(id, onDone = ::synced) // queued before the list's sync of every game
        refreshLibrary() // it is now first, with the picture of where it was left
    }
```

`restartGame()`: `open(g, resumeAuto = false, sync = false)`.

`onPause()`: after `saveAuto()`:

```kotlin
        val id = heldId
        if (playing && id != null && cloudOn()) syncWorker.game(id, syncFiles.read(id), ::synced) // paused, not quit: upload only
```

`changeSettings(s)`: after `fastSpeed = s.fastSpeed` add `cloudService = s.cloud`.

The cloud helpers (after `pickOnlineCheats`):

```kotlin
    private fun cloudOn() = settings.cloud != Settings.CLOUD_OFF && !cloudPrefs.stopped

    private fun serviceName() = Settings.CLOUD_NAMES[settings.cloud]

    /** The phone's name as the user set it (the clash pop-up on the other phone shows it). */
    private fun phoneName(): String =
        android.provider.Settings.Global.getString(contentResolver, android.provider.Settings.Global.DEVICE_NAME) ?: android.os.Build.MODEL

    /** The sync for the chosen service, on the sync thread; null: off or stopped. */
    private fun newGameSync(): GameSync? {
        if (cloudPrefs.stopped) return null
        val drive: CloudDrive = when (cloudService) {
            Settings.CLOUD_GOOGLE -> GoogleDrive(googleAuth::token, Net::send)
            else -> return null
        }
        return GameSync(drive, syncFiles, phoneName(), cloudPrefs.phoneId)
    }

    /** Every game whose id is known, with the cloud (the list shows; 10c). */
    private fun syncAll() {
        if (!cloudOn() || playing) return
        val ids = library.games.map { it.id }.filter { it.isNotEmpty() }.distinct()
        if (ids.isNotEmpty()) syncWorker.all(ids, ::synced)
    }

    /** A sync job finished: the time kept, a problem said once, the list's pictures read again. */
    private fun synced(done: SyncWorker.Done) = runOnUiThread {
        when (done.problem) {
            null -> if (done.results.isNotEmpty()) {
                cloudPrefs.lastSync = System.currentTimeMillis()
                cloudPrefs.save()
            }
            CloudDrive.Kind.SIGNED_OUT -> if (!cloudPrefs.stopped) {
                cloudPrefs.stopped = true
                cloudPrefs.save()
                toast("Cloud sync stopped. Sign in again in Settings.")
            }
            CloudDrive.Kind.FULL -> if (!fullSaid) {
                fullSaid = true
                toast("${serviceName()} is full. Saves stay on this phone.")
            }
            CloudDrive.Kind.FAILED -> {}
        }
        cloudStatus = CloudStatus(cloudPrefs.lastSync, cloudPrefs.stopped)
        if (done.results.values.any { it == GameSync.Result.Downloaded }) libraryRefresh++
    }

    /** Settings → Cloud sync picked [service] (or Sign in again): signed in first; Off disconnects. */
    private fun chooseCloud(service: Int) {
        if (service == settings.cloud && !cloudPrefs.stopped) return
        if (settings.cloud != Settings.CLOUD_OFF && service != settings.cloud) disconnect()
        when (service) {
            Settings.CLOUD_GOOGLE -> googleAuth.signIn(this,
                onAsk = { googleAsk.launch(IntentSenderRequest.Builder(it.intentSender).build()) }, onDone = ::googleSignedIn)
        }
    }

    private fun googleSignedIn(ok: Boolean) {
        if (ok) connected(Settings.CLOUD_GOOGLE) else toast("Couldn't sign in to Google Drive.")
    }

    /** Signed in: sync is on with [service], and every game is synced (a first connect, spec §5). */
    private fun connected(service: Int) {
        cloudPrefs.stopped = false
        cloudPrefs.save()
        changeSettings(settings.copy(cloud = service))
        cloudStatus = CloudStatus(cloudPrefs.lastSync, false)
        syncAll()
    }

    /** Sync off; everything stays on the phone and in the cloud; connecting again is a first connect. */
    private fun disconnect() {
        changeSettings(settings.copy(cloud = Settings.CLOUD_OFF))
        syncWorker.then {
            syncFiles.forgetAll()
            cloudPrefs.lastSync = 0
            cloudPrefs.stopped = false
            cloudPrefs.save()
        }
        cloudStatus = CloudStatus()
    }
```

and, as the last thing in the class (`MainActivity` has no companion yet):

```kotlin
    companion object {
        const val OPEN_WAIT_MS = 15_000L // opening a game waits this long for its sync (spec §2)
    }
```

- [ ] **Step 3: Build and run all the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: Kotlin 196, 0 failures (`UiTextTest` included).
Run: `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest` — Expected: on-device 73, 0 failures (opening games without cloud sync is unchanged).

- [ ] **Step 4: Sign in on the emulator (the user's Google account added in Task 1)**

`bash tools/dev.sh install && bash tools/dev.sh launch`; Settings → Cloud sync → Google Drive. Google's pop-up appears: **ask the user in chat before tapping Allow** (or let them tap it). Expected: the page says "Connected to Google Drive" and, within a few seconds, a "Last synced today …" line.

- [ ] **Step 5: See an upload**

Open hack 1, play ~5 s, Back → Quit. Then:
Run: `adb shell run-as com.tengen.gbaemu cat files/sync/e86b3224.json`
Expected: a receipt with `"phone"` the emulator's name, `"count": 1` (or higher), and the save and auto files listed. Open hack 1 again: no spinner longer than a moment, no clash (nothing changed elsewhere).

- [ ] **Step 6: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/GoogleAuth.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md
git commit -m "feat: cloud sync with Google Drive in the app

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Dropbox (`DropboxDrive`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/DropboxDrive.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/DropboxDriveTest.kt` (new, 6 tests)

- [ ] **Step 1: Write the failing test**

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class DropboxDriveTest {
    class Asked(val url: String, val headers: Map<String, String>, val body: ByteArray?)

    private val asked = mutableListOf<Asked>()
    private val replies = ArrayDeque<Net.Reply>()
    private val send: Send = { method, url, headers, body, _ ->
        assertEquals("POST", method) // every Dropbox call is a POST
        asked += Asked(url, headers, body)
        replies.removeFirstOrNull() ?: Net.Reply(500, ByteArray(0))
    }
    private val tokens = mutableListOf<Boolean>()
    private var token: String? = "t1"
    private val drive = DropboxDrive({ fresh -> tokens += fresh; if (fresh && token != null) "t2" else token }, send)

    private fun reply(status: Int, text: String = "") = replies.add(Net.Reply(status, text.toByteArray()))
    private fun problem(kind: CloudDrive.Kind, block: () -> Unit) {
        try {
            block()
            fail("expected $kind")
        } catch (p: CloudDrive.Problem) {
            assertEquals(kind, p.kind)
        }
    }

    @Test
    fun listReadsEveryPageOfFilesOnly() {
        reply(200, """{"entries": [{".tag": "folder", "path_display": "/e86b3224", "path_lower": "/e86b3224"},
            {".tag": "file", "path_display": "/e86b3224/receipt.json", "path_lower": "/e86b3224/receipt.json"}],
            "cursor": "c1", "has_more": true}""")
        reply(200, """{"entries": [{".tag": "file", "path_display": "/1f1c08fb/receipt.json", "path_lower": "/1f1c08fb/receipt.json"}],
            "cursor": "c2", "has_more": false}""")
        assertEquals(listOf(CloudDrive.Entry("e86b3224/receipt.json", "/e86b3224/receipt.json")), drive.list("e86b3224/"))
        assertEquals(DropboxDrive.API + "files/list_folder", asked[0].url)
        assertEquals("Bearer t1", asked[0].headers["Authorization"])
        assertEquals("application/json", asked[0].headers["Content-Type"])
        assertTrue(String(asked[0].body!!), "\"recursive\": true" in String(asked[0].body!!))
        assertEquals(DropboxDrive.API + "files/list_folder/continue", asked[1].url)
        assertEquals("{\"cursor\": \"c1\"}", String(asked[1].body!!))
    }

    @Test
    fun aMissingAppFolderListsEmpty() {
        reply(409, """{"error_summary": "path/not_found/.."}""")
        assertTrue(drive.list("").isEmpty())
    }

    @Test
    fun uploadOverwritesAtThePath() {
        reply(200, "{}")
        drive.upload("e86b3224/receipt.json", "R".toByteArray())
        val a = asked.single()
        assertEquals(DropboxDrive.CONTENT + "files/upload", a.url)
        assertEquals("{\"path\": \"/e86b3224/receipt.json\", \"mode\": \"overwrite\", \"mute\": true}", a.headers["Dropbox-API-Arg"])
        assertEquals("application/octet-stream", a.headers["Content-Type"])
        assertArrayEquals("R".toByteArray(), a.body)
    }

    @Test
    fun downloadReadsTheBytesAndANotFoundIsNull() {
        reply(200, "bytes")
        assertArrayEquals("bytes".toByteArray(), drive.download(CloudDrive.Entry("a/x", "/a/x")))
        assertEquals(DropboxDrive.CONTENT + "files/download", asked[0].url)
        assertEquals("{\"path\": \"/a/x\"}", asked[0].headers["Dropbox-API-Arg"])
        assertNull(asked[0].body)
        reply(409, """{"error_summary": "path/not_found/"}""")
        assertNull(drive.download(CloudDrive.Entry("a/x", "/a/x")))
    }

    @Test
    fun deletingAGoneFileIsFine() {
        reply(200, "{}")
        drive.delete(CloudDrive.Entry("a/x", "/a/x"))
        assertEquals(DropboxDrive.API + "files/delete_v2", asked[0].url)
        reply(409, """{"error_summary": "path_lookup/not_found/"}""")
        drive.delete(CloudDrive.Entry("a/x", "/a/x"))
    }

    @Test
    fun aRefusedTokenAFullDropboxAndFailures() {
        reply(401)
        reply(200, """{"entries": [], "cursor": "c", "has_more": false}""")
        drive.list("")
        assertEquals(listOf(false, true), tokens)
        reply(401)
        reply(401)
        problem(CloudDrive.Kind.SIGNED_OUT) { drive.list("") }
        token = null
        problem(CloudDrive.Kind.SIGNED_OUT) { drive.list("") }
        token = "t1"
        reply(409, """{"error_summary": "path/insufficient_space/"}""")
        problem(CloudDrive.Kind.FULL) { drive.upload("a/x", ByteArray(1)) }
        reply(429)
        problem(CloudDrive.Kind.FAILED) { drive.list("") }
        replies.add(Net.Reply(0, null))
        problem(CloudDrive.Kind.FAILED) { drive.download(CloudDrive.Entry("a/x", "/a/x")) }
    }
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile error (`DropboxDrive` unknown).

- [ ] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/DropboxDrive.kt`:

```kotlin
package com.tengen.gbaemu

/**
 * [CloudDrive] in Dropbox's app folder (10c; HTTP API v2, an App-folder app): a path is the file's path under the
 * folder. [token] gives an access token (true: a fresh one, the last was refused); null: the user must sign in again.
 */
class DropboxDrive(private val token: (fresh: Boolean) -> String?, private val send: Send) : CloudDrive {
    override fun list(prefix: String): List<CloudDrive.Entry> {
        val out = mutableListOf<CloudDrive.Entry>()
        var r = call(API + "files/list_folder", "{\"path\": \"\", \"recursive\": true, \"limit\": 2000}".toByteArray(), JSON)
        if (r.status == 409) return out // no app folder yet
        while (true) {
            val m = read(r)
            try {
                for (e in m["entries"] as List<*>) {
                    e as Map<*, *>
                    if (e[".tag"] != "file") continue
                    val path = (e["path_display"] as String).removePrefix("/")
                    if (path.startsWith(prefix)) out += CloudDrive.Entry(path, e["path_lower"] as String)
                }
            } catch (x: Exception) {
                throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
            }
            if (m["has_more"] != true) return out
            r = call(API + "files/list_folder/continue", "{\"cursor\": ${Json.str(m["cursor"] as? String ?: "")}}".toByteArray(), JSON)
            if (r.status == 409) throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
        }
    }

    override fun download(entry: CloudDrive.Entry): ByteArray? {
        val r = call(CONTENT + "files/download", null, null, "{\"path\": ${Json.str(entry.ref)}}", GoogleDrive.MAX_FILE)
        return if (r.status == 409) null else r.body
    }

    override fun upload(path: String, bytes: ByteArray) {
        val r = call(CONTENT + "files/upload", bytes, "application/octet-stream",
            "{\"path\": ${Json.str("/$path")}, \"mode\": \"overwrite\", \"mute\": true}")
        if (r.status == 409) throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
    }

    override fun delete(entry: CloudDrive.Entry) {
        call(API + "files/delete_v2", "{\"path\": ${Json.str(entry.ref)}}".toByteArray(), JSON) // 409: gone already
    }

    private fun read(r: Net.Reply): Map<*, *> = try {
        Json.Reader(r.text).run { (value() as Map<*, *>).also { end() } }
    } catch (e: Exception) {
        throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
    }

    /** One POST with the token; a refused token is renewed once. 2xx and 409 (Dropbox's "not found" and such) come back. */
    private fun call(url: String, body: ByteArray?, type: String?, arg: String? = null, max: Int = 1 shl 20): Net.Reply {
        var fresh = false
        while (true) {
            val t = token(fresh) ?: throw CloudDrive.Problem(CloudDrive.Kind.SIGNED_OUT)
            val headers = HashMap<String, String>()
            headers["Authorization"] = "Bearer $t"
            if (type != null) headers["Content-Type"] = type
            if (arg != null) headers["Dropbox-API-Arg"] = arg
            val r = send("POST", url, headers, body, max)
            when {
                r.status == 401 && !fresh -> fresh = true
                r.status == 401 -> throw CloudDrive.Problem(CloudDrive.Kind.SIGNED_OUT)
                "insufficient_space" in r.text -> throw CloudDrive.Problem(CloudDrive.Kind.FULL)
                r.status == 409 -> return r
                r.status in 200..299 && r.body != null -> return r
                else -> throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
            }
        }
    }

    companion object {
        const val API = "https://api.dropboxapi.com/2/"
        const val CONTENT = "https://content.dropboxapi.com/2/"
        private const val JSON = "application/json"
    }
}
```

- [ ] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 202, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/DropboxDrive.kt app/src/test/java/com/tengen/gbaemu/DropboxDriveTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md
git commit -m "feat: Dropbox's app folder as the cloud (DropboxDrive)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Signing in to Dropbox (`DropboxAuth`)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/DropboxAuth.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/DropboxAuthTest.kt` (new, 6 tests)

- [ ] **Step 1: Write the failing test**

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class DropboxAuthTest {
    private val file = File(Files.createTempDirectory("dropbox").toFile(), "sync/dropbox.json")
    private val bodies = mutableListOf<String>()
    private val replies = ArrayDeque<Net.Reply>()
    private val send: Send = { method, url, headers, body, _ ->
        assertEquals("POST", method)
        assertEquals(DropboxAuth.TOKEN, url)
        assertEquals("application/x-www-form-urlencoded", headers["Content-Type"])
        bodies += String(body!!)
        replies.removeFirstOrNull() ?: Net.Reply(500, ByteArray(0))
    }
    private var clock = 1_000_000L
    private fun auth() = DropboxAuth(file, "KEY", send) { clock }
    private fun reply(status: Int, text: String) = replies.add(Net.Reply(status, text.toByteArray()))
    private fun signedIn(): DropboxAuth {
        val a = auth()
        a.start()
        reply(200, """{"access_token": "a1", "expires_in": 14400, "refresh_token": "r1", "token_type": "bearer"}""")
        assertTrue(a.finish("db-KEY://auth?code=abc"))
        return a
    }

    @Test
    fun theChallengeIsRfc7636s() {
        assertEquals("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", DropboxAuth.challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"))
    }

    @Test
    fun startMakesTheSignInLinkAndKeepsTheVerifier() {
        val link = auth().start()
        assertTrue(link, link.startsWith(DropboxAuth.AUTHORIZE + "?client_id=KEY"))
        for (part in listOf("response_type=code", "code_challenge_method=S256", "token_access_type=offline", "redirect_uri=db-KEY%3A%2F%2Fauth")) {
            assertTrue(link, part in link)
        }
        reply(200, """{"access_token": "a1", "expires_in": 14400, "refresh_token": "r1"}""")
        assertTrue(auth().finish("db-KEY://auth?code=abc")) // another instance: the verifier was kept in the file
    }

    @Test
    fun finishTradesTheCodeForARefreshToken() {
        val a = signedIn()
        val body = bodies.single()
        assertTrue(body, "code=abc" in body)
        assertTrue(body, "grant_type=authorization_code" in body)
        assertTrue(body, "code_verifier=" in body)
        assertTrue(body, "client_id=KEY" in body)
        assertTrue(a.signedIn)
        assertEquals("a1", a.token(false))
        assertEquals(1, bodies.size) // still fresh: no request
        assertTrue(auth().signedIn) // kept in the file
    }

    @Test
    fun anErrorOrAnotherLinkIsRefused() {
        val a = auth()
        a.start()
        assertFalse(a.finish("db-KEY://auth?error=access_denied"))
        assertFalse(a.finish("https://example.com/?code=1"))
        assertFalse(a.signedIn)
        assertNull(a.token(false))
        assertTrue(bodies.isEmpty())
    }

    @Test
    fun anOldTokenIsRenewedAndARefusedRenewalSignsOut() {
        val a = signedIn()
        clock += 14_400_000L
        reply(200, """{"access_token": "a2", "expires_in": 14400}""")
        assertEquals("a2", a.token(false))
        assertTrue(bodies.last(), "grant_type=refresh_token" in bodies.last() && "refresh_token=r1" in bodies.last())
        reply(200, """{"access_token": "a3", "expires_in": 14400}""")
        assertEquals("a3", a.token(true)) // refused by Dropbox: a fresh one even though not expired
        reply(400, """{"error": "invalid_grant"}""")
        assertNull(a.token(true))
        assertFalse(a.signedIn)
        assertFalse(auth().signedIn)
    }

    @Test
    fun noInternetWhileRenewingIsAFailureNotASignOut() {
        val a = signedIn()
        replies.add(Net.Reply(0, null))
        try {
            a.token(true)
            fail("expected FAILED")
        } catch (p: CloudDrive.Problem) {
            assertEquals(CloudDrive.Kind.FAILED, p.kind)
        }
        assertTrue(a.signedIn)
    }
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest` — Expected: compile error (`DropboxAuth` unknown).

- [ ] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/DropboxAuth.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.net.URLDecoder
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.Base64

/**
 * Signing in to Dropbox (10c): the OAuth code flow with PKCE, no secret in the app. [start] makes the sign-in link
 * (the verifier kept in [file], in case Android closes the app meanwhile); the browser comes back to
 * `db-<app key>://auth?code=…`, which [finish] trades for a refresh token. [token] gives a 4-hour access token,
 * renewed when needed. Kept in `files/sync/dropbox.json`.
 */
class DropboxAuth(private val file: File, private val appKey: String, private val send: Send,
                  private val clock: () -> Long = System::currentTimeMillis) {
    private var verifier = ""
    private var refresh = ""
    private var access = ""
    private var expires = 0L
    private val random = SecureRandom()

    val redirect: String get() = "db-$appKey://auth"
    val signedIn: Boolean
        @Synchronized get() = refresh.isNotEmpty()

    init {
        try {
            val m = Json.Reader(file.readText()).value() as Map<*, *>
            verifier = m["verifier"] as String
            refresh = m["refresh"] as String
            access = m["access"] as String
            expires = (m["expires"] as Double).toLong()
        } catch (e: Exception) { // none yet, or damaged: signed out
        }
    }

    /** A new sign-in: the link to open in the browser. */
    @Synchronized fun start(): String {
        verifier = buildString { repeat(64) { append(CHARS[random.nextInt(CHARS.length)]) } }
        save()
        return AUTHORIZE + "?" + Net.form("client_id" to appKey, "response_type" to "code", "code_challenge" to challenge(verifier),
            "code_challenge_method" to "S256", "token_access_type" to "offline", "redirect_uri" to redirect)
    }

    /** The browser's return [link]: true if it carried a code that became a refresh token. */
    @Synchronized fun finish(link: String): Boolean {
        if (!link.startsWith("$redirect?") || verifier.isEmpty()) return false
        val code = query(link)["code"] ?: return false
        val m = post(Net.form("code" to code, "grant_type" to "authorization_code", "code_verifier" to verifier,
            "client_id" to appKey, "redirect_uri" to redirect)) ?: return false
        refresh = m["refresh_token"] as? String ?: return false
        keep(m)
        verifier = ""
        save()
        return true
    }

    /**
     * An access token ([fresh]: the last one was refused); null: signed out (no refresh token, or Dropbox refused
     * it — access removed). Dropbox out of reach throws [CloudDrive.Kind.FAILED].
     */
    @Synchronized fun token(fresh: Boolean): String? {
        if (refresh.isEmpty()) return null
        if (!fresh && access.isNotEmpty() && clock() < expires - 60_000) return access
        val r = send("POST", TOKEN, FORM, Net.form("grant_type" to "refresh_token", "refresh_token" to refresh,
            "client_id" to appKey).toByteArray(), 1 shl 16)
        if (r.status == 400 || r.status == 401) {
            signOut()
            return null
        }
        val m = parse(r) ?: throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
        if (m["access_token"] !is String) throw CloudDrive.Problem(CloudDrive.Kind.FAILED)
        keep(m)
        save()
        return access
    }

    @Synchronized fun signOut() {
        refresh = ""
        access = ""
        expires = 0
        save()
    }

    private fun keep(m: Map<*, *>) {
        access = m["access_token"] as? String ?: ""
        expires = clock() + ((m["expires_in"] as? Double)?.toLong() ?: 0L) * 1000
    }

    private fun post(form: String): Map<*, *>? = parse(send("POST", TOKEN, FORM, form.toByteArray(), 1 shl 16))

    private fun parse(r: Net.Reply): Map<*, *>? = if (r.status != 200) null else try {
        Json.Reader(r.text).value() as Map<*, *>
    } catch (e: Exception) {
        null
    }

    private fun save() {
        file.parentFile?.mkdirs()
        file.writeText("{\"verifier\": ${Json.str(verifier)}, \"refresh\": ${Json.str(refresh)}, " +
            "\"access\": ${Json.str(access)}, \"expires\": $expires}")
    }

    companion object {
        const val AUTHORIZE = "https://www.dropbox.com/oauth2/authorize"
        const val TOKEN = "https://api.dropboxapi.com/oauth2/token"
        private val FORM = mapOf("Content-Type" to "application/x-www-form-urlencoded")
        private const val CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"

        /** PKCE's S256 challenge: the verifier's SHA-256, base64url without padding (RFC 7636). */
        fun challenge(verifier: String): String = Base64.getUrlEncoder().withoutPadding()
            .encodeToString(MessageDigest.getInstance("SHA-256").digest(verifier.toByteArray(Charsets.US_ASCII)))

        /** A link's query as name → value. */
        private fun query(link: String): Map<String, String> = link.substringAfter('?').split('&').mapNotNull { part ->
            val i = part.indexOf('=')
            if (i <= 0) null else part.substring(0, i) to URLDecoder.decode(part.substring(i + 1), "UTF-8")
        }.toMap()
    }
}
```

- [ ] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: PASS; Kotlin 208, 0 failures (`UiTextTest`: the `&` in `query` is a Char).

- [ ] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/DropboxAuth.kt app/src/test/java/com/tengen/gbaemu/DropboxAuthTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md
git commit -m "feat: signing in to Dropbox with PKCE (DropboxAuth)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Dropbox in the app (the return link and the wiring)

**Files:**
- Modify: `app/build.gradle.kts`, `app/src/main/AndroidManifest.xml`, `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`

- [ ] **Step 1: The key into the app and the manifest**

In `app/build.gradle.kts`, inside `defaultConfig { … }` after `testInstrumentationRunner = …`:

```kotlin
        // Cloud sync's Dropbox app key (gradle.properties; public with PKCE): the code and the return link use it.
        val dropboxKey = providers.gradleProperty("dropboxKey").get()
        buildConfigField("String", "DROPBOX_KEY", "\"$dropboxKey\"")
        manifestPlaceholders["dropboxScheme"] = "db-$dropboxKey"
```

and `buildFeatures { compose = true }` → `buildFeatures { compose = true; buildConfig = true }`.

In `AndroidManifest.xml`, inside the `<activity>` after the launcher `<intent-filter>`:

```xml
            <!-- Dropbox's sign-in comes back here (10c): db-<app key>://auth?code=… -->
            <intent-filter>
                <action android:name="android.intent.action.VIEW" />
                <category android:name="android.intent.category.DEFAULT" />
                <category android:name="android.intent.category.BROWSABLE" />
                <data android:scheme="${dropboxScheme}" android:host="auth" />
            </intent-filter>
```

- [ ] **Step 2: The wiring in `MainActivity.kt`**

Field after `googleAuth`: `private lateinit var dropboxAuth: DropboxAuth`. In `onCreate` after `googleAuth = …`:

```kotlin
        dropboxAuth = DropboxAuth(File(filesDir, "sync/dropbox.json"), BuildConfig.DROPBOX_KEY, Net::send)
```

and at the end of `onCreate` (after `setContent { … }`): `dropboxReturned(intent)`.

`newGameSync()`: add the branch `Settings.CLOUD_DROPBOX -> DropboxDrive(dropboxAuth::token, Net::send)` before `else`.

`chooseCloud(...)`'s `when`: add

```kotlin
            Settings.CLOUD_DROPBOX -> try {
                startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(dropboxAuth.start())))
            } catch (e: Exception) {
                toast("Couldn't open the browser to sign in to Dropbox.")
            }
```

`disconnect()`: capture `val was = settings.cloud` first, and inside `syncWorker.then { … }` add `if (was == Settings.CLOUD_DROPBOX) dropboxAuth.signOut()`.

New overrides and helper (after `onResume`):

```kotlin
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        dropboxReturned(intent)
    }

    /** Dropbox's sign-in came back (db-<key>://auth?code=…): the code becomes a refresh token, off the UI thread. */
    private fun dropboxReturned(intent: Intent?) {
        val link = intent?.data?.toString() ?: return
        if (!link.startsWith(dropboxAuth.redirect)) return
        setIntent(Intent(intent).setData(null)) // not handled again when the activity is made again
        thread(name = "dropbox-sign-in") {
            val ok = dropboxAuth.finish(link)
            runOnUiThread { if (ok) connected(Settings.CLOUD_DROPBOX) else toast("Couldn't sign in to Dropbox.") }
        }
    }
```

- [ ] **Step 3: Build and run all the tests**

Run: `bash tools/dev.sh jvmtest` — Expected: Kotlin 208, 0 failures.
Run: `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest` — Expected: on-device 73, 0 failures.
Run: `bash tools/dev.sh install && adb shell dumpsys package com.tengen.gbaemu | grep -A2 "db-"`
Expected: the activity's filter lists the scheme `db-<key>` and host `auth`.

- [ ] **Step 4: Commit**

```bash
git add app/build.gradle.kts app/src/main/AndroidManifest.xml app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-04-milestone-10c-cloud-sync.md
git commit -m "feat: cloud sync with Dropbox in the app

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Claude's check, the user's check, milestone 10c exit

**Files:** docs only (`docs/PROGRESS.md`, this plan, the spec's status, the parent spec's status line and §10 row 10, `CLAUDE.md` key files).

- [ ] **Step 1: Full runs**

Run: `bash tools/dev.sh test` — Expected: CTest 120/120.
Run: `bash tools/dev.sh jvmtest` — Expected: Kotlin 208, 0 failures.
Run: `rm -rf app/build/outputs/androidTest-results && bash tools/dev.sh androidtest` — Expected: on-device 73, 0 failures.

- [ ] **Step 2: Back up the emulator app's data first**

`adb shell run-as com.tengen.gbaemu ls files` and copy out `files/library.json`, `files/cheats/`, `files/states/`, `files/saves/` and `shared_prefs/settings.xml` (as in 10b's check: `adb exec-out run-as com.tengen.gbaemu cat <file> > <scratchpad>/<file>`), to put back at the end.

- [ ] **Step 3: Ask the user (stop) — the second device and the sign-ins**

Ask in one message: OK to install the app on the **phone A** (`bash tools/dev.sh phone-install`, USB debugging on, only that phone plugged in) and copy hack 1 to it (`bash tools/dev.sh phone-push "<hack 1 path>"`), or the user does that; the user signs in on both (Google: the account pop-up's Allow is theirs; Dropbox: the browser sign-in and Allow are theirs).

- [ ] **Step 4: Claude's check with Google Drive** (emulator = E, phone = P, both Google Drive, same account)

1. E: play hack 1 ~10 s, quit → `files/sync/e86b3224.json` on E has E's name.
2. P: open hack 1 → the spinner briefly, then the game resumes at E's moment (Continue picture = E's). Quit.
3. Clash: airplane mode on E (`adb shell cmd connectivity airplane-mode enable`); play on E and quit; play on P and quit (P uploads); airplane off on E; open hack 1 on E → the pop-up shows both phones' names, pictures, times. Pick **P's** → E plays P's moment; E's `files/sync-backup/e86b3224/` holds E's own set.
4. Do 3 again and pick **this phone's** (E) → P, opening hack 1, gets E's moment; E's backup holds P's set.
5. Airplane mode on E, open hack 1 → "Couldn't reach Google Drive. Playing this phone's save."; play, quit; airplane off, back to the list → it uploads by itself (E's receipt `count` goes up).
6. Settings → Cloud sync → Disconnect on E → "Off"; `files/sync/*.json` (the 8-hex ones) gone, saves and states untouched.

- [ ] **Step 5: Claude's check with Dropbox** — the same steps 1–4 with Dropbox chosen on both (E's browser sign-in by the user), plus: the Dropbox website shows `Apps/<app name>/e86b3224/` with `receipt.json` and the hash-named files.

- [ ] **Step 6: Put the emulator back**

Disconnect on E; put the Step 2 backups back (check identical); airplane mode off. Note in PROGRESS.md anything else that changed (e.g. hack 1's Continue moment).

- [ ] **Step 7: The user's check (stop)**

Ask the user to: play game A on one phone, continue on the other — once with Google Drive, once with Dropbox — and make one clash on purpose, picking a side. Wait for their answer; fix anything they find (systematic-debugging) before going on.

- [ ] **Step 8: Docs, tag, merge question**

Update: the spec's status → implemented; the parent spec's status line and §10 row 10 → 10c ✅, milestone 10 complete; `CLAUDE.md` key files (current spec / plan → none, last finished = 10c's); `PROGRESS.md` (Completed, Project status, Next, Notes: the Google Cloud project, the Dropbox app, `dev.sh online`); every box of this plan ticked. Commit, then `git tag milestone-10c` and ask the user whether to merge into `main` (superpowers:finishing-a-development-branch).
