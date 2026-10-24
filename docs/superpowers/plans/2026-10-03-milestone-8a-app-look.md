# Milestone 8a — The App's New Look — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** The app's own look ('Cartridge grey'), the game list as a grid with a Continue card, a clearer pause menu and Settings, real icons and no "…" in any label — nothing the app does changes.

**Architecture:** One theme file (`AppTheme.kt`) holds the colour scheme, shapes and a few reusable pieces (header, menu tile, choice buttons, switch row, section row, bottom sheet); every screen is rebuilt on them. Pure logic (`Library.continueAndRest`) is tested on the PC, the screens by Compose UI tests on the emulator, and a PC test reads every string literal so no "…", "&" or symbol icon slips back.

**Tech Stack:** Kotlin, Jetpack Compose (Material 3 1.4.0, material-icons-extended 1.7.8), Compose UI test 1.11.4 + Espresso 3.7.0, JUnit 4, Gradle `--offline`, Git Bash `tools/dev.sh`.

**Spec:** `docs/superpowers/specs/2026-10-03-milestone-8a-app-look-design.md`

---

## Conventions

- Git Bash from the repo root. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest>` (the Pixel 9 phone emulator `Pixel_9_API_36_1`; never the user's USB phones unless the user asks). Gradle runs `--offline`; every library this plan needs is in the PC's Gradle cache (the user allowed downloading the test libraries on 2026-10-03). A missing artifact → stop and ask the user before any download. Ask before restarting or cold-booting the emulator.
- The core (C) is not touched: CTest stays 120/120 (run once in Task 6).
- Counting tests: Kotlin unit tests `ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'` (total, failures); on-device `grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml` after `rm -rf app/build/outputs/androidTest-results`.
- Test-first where a test can fail first: the Continue-card tests before `continueAndRest` (they don't compile without it), the screen tests before the screens (the pause menu's new signature doesn't compile on today's code; the Settings test fails on today's pick dialogs). The wording test (Task 5) can only pass once every screen is done; the prototype proved it catches a planted "Settings…".
- One commit per task; messages end with the Co-Authored-By trailer. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes). The tasks run one after another without stopping; the only stop is Task 6, the user's check. After every task: all documents updated and checked for anything stale.
- This plan's code is the prototype's (worktree `<scratchpad>/p8a`, branch `proto-8a`, `e7da92b`), which the user tried on the emulator on 2026-10-03 ('Everything works'). After Task 5, `git diff proto-8a -- app` is empty.

## File map

```
app/build.gradle.kts; app/src/main/java/com/tengen/gbaemu/AppTheme.kt (new)                                              Task 1
app/src/main/java/com/tengen/gbaemu/Library.kt; app/src/test/java/com/tengen/gbaemu/LibraryTest.kt                                    Task 2
app/src/main/java/com/tengen/gbaemu/Screens.kt, MainActivity.kt; app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt (new)      Task 3
app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt, LayoutEditor.kt, TouchControlsView.kt, TouchLayout.kt, MotionInput.kt     Task 4
app/src/test/java/com/tengen/gbaemu/UiTextTest.kt (new)                                              Task 5
```

---
### Task 1: The theme, the icons and the test libraries

**Files:**
- Modify: `app/build.gradle.kts` (`material-icons-extended-android` 1.7.8; androidTest: `ui-test-junit4-android` 1.11.4, `espresso-core` 3.7.0 — 3.5, the default, fails on Android 14+ with `NoSuchMethodException: InputManager.getInstance`; debug: `ui-test-manifest` 1.11.4)
- Create: `app/src/main/java/com/tengen/gbaemu/AppTheme.kt` (`AppColors`, `AppShapes`, `AppTheme`, `ScreenHeader`, `MenuTile`, `Choices`, `Help`, `SwitchRow`, `ChoiceRow`, `IconBadge`, `NavRow`, `SheetItem`, `ActionSheet`, `cardBorder`; `ContinueBorder`, `Scrim`)

(Nothing uses it yet; its check is that the app builds offline and today's tests pass. Note from the prototype: `surfaceVariant` must differ from `surfaceContainer` — Material picks a container's text colour by matching its colour value, and the same value made card text grey.)

- [x] **Step 1: Write the files**

Replace `app/build.gradle.kts`:

```kotlin
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.tengen.gbaemu"
    compileSdk = 36
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.tengen.gbaemu"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += listOf("x86_64", "arm64-v8a") }
        // -PgbaForceGeneric=ON: the arm64 core with only the plain-C twins of its fast paths (tools/dev.sh GENERIC=1)
        externalNativeBuild { cmake { arguments += "-DGBA_FORCE_GENERIC=${project.findProperty("gbaForceGeneric") ?: "OFF"}" } }
    }

    externalNativeBuild {
        cmake {
            path = file("../platform/android/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    buildFeatures { compose = true }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // On-device tests read the local test ROMs + approved fingerprints from the test APK's assets
    // (copied from the gitignored tests/ folders at build time; never in the app APK or in git).
    sourceSets.getByName("androidTest").assets.srcDir(layout.buildDirectory.dir("generated/testRoms").get().asFile)
}

kotlin {
    compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) }
}

val copyTestRoms by tasks.registering(Copy::class) {
    from(rootProject.file("tests")) {
        include("expected_frames.json", "roms/jsmolka/*.gba", "roms/tonc/*.gba", "own_roms/*.gba")
    }
    into(layout.buildDirectory.dir("generated/testRoms"))
}
tasks.matching { it.name == "mergeDebugAndroidTestAssets" }.configureEach { dependsOn(copyTestRoms) }

dependencies {
    implementation("androidx.core:core-ktx:1.17.0")
    implementation("androidx.activity:activity-compose:1.12.3")
    implementation("androidx.compose.ui:ui-android:1.11.4")
    implementation("androidx.compose.foundation:foundation-android:1.11.4")
    implementation("androidx.compose.material3:material3-android:1.4.0")
    implementation("androidx.compose.material:material-icons-extended-android:1.7.8") // the menus' icons (8a)
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("androidx.compose.ui:ui-test-junit4-android:1.11.4") // screen tests (8a)
    androidTestImplementation("androidx.test.espresso:espresso-core:3.7.0") // 3.5 (the default) fails on Android 14+
    debugImplementation("androidx.compose.ui:ui-test-manifest:1.11.4")
}
```

Create `app/src/main/java/com/tengen/gbaemu/AppTheme.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.Shapes
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * The app's look (milestone 8a, "Cartridge grey"): charcoal like a console's shell, one amber accent, soft red for
 * what can't be undone. Always dark, whatever the phone's setting. Screens read these roles, never their own colours.
 */
val AppColors = darkColorScheme(
    primary = Color(0xFFF2A33A), onPrimary = Color(0xFF2A1A02),
    primaryContainer = Color(0xFF3A2F1C), onPrimaryContainer = Color(0xFFF2A33A),
    secondary = Color(0xFFF2A33A), onSecondary = Color(0xFF2A1A02),
    secondaryContainer = Color(0xFF3A2F1C), onSecondaryContainer = Color(0xFFF7D9AE),
    background = Color(0xFF1A1B1E), onBackground = Color(0xFFECEAE4),
    surface = Color(0xFF1A1B1E), onSurface = Color(0xFFECEAE4),
    surfaceVariant = Color(0xFF303237), onSurfaceVariant = Color(0xFF9C998F),
    surfaceContainerLowest = Color(0xFF151619), surfaceContainerLow = Color(0xFF202125),
    surfaceContainer = Color(0xFF2A2C31), surfaceContainerHigh = Color(0xFF33302A), surfaceContainerHighest = Color(0xFF3A3C42),
    error = Color(0xFFF2A0A0), onError = Color(0xFF3A1F1F), errorContainer = Color(0xFF3A1F1F), onErrorContainer = Color(0xFFF2A0A0),
    outline = Color(0xFF75736C), outlineVariant = Color(0xFF2E3035),
)

/** The Continue card's border and the dimmed layer behind menus. */
val ContinueBorder = Color(0xFF5A4A2A)
val Scrim = Color(0xB0000000)

val AppShapes = Shapes(
    extraSmall = RoundedCornerShape(8.dp), small = RoundedCornerShape(12.dp), medium = RoundedCornerShape(12.dp),
    large = RoundedCornerShape(16.dp), extraLarge = RoundedCornerShape(16.dp),
)

@Composable
fun AppTheme(content: @Composable () -> Unit) = MaterialTheme(colorScheme = AppColors, shapes = AppShapes, content = content)

/** A screen's top bar: a back arrow (if [onBack]), the title (and a smaller line under it), icon buttons on the right. */
@Composable
fun ScreenHeader(title: String, onBack: (() -> Unit)? = null, subtitle: String? = null, actions: @Composable RowScope.() -> Unit = {}) {
    Row(Modifier.fillMaxWidth().heightIn(min = 56.dp).padding(start = if (onBack == null) 16.dp else 4.dp, end = 4.dp),
        verticalAlignment = Alignment.CenterVertically) {
        if (onBack != null) {
            IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") }
        }
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleLarge, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (subtitle != null) {
                Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
        actions()
    }
}

/** A square-ish tile with an icon over its label: the pause menu's main choices. */
@Composable
fun MenuTile(icon: ImageVector, label: String, onClick: () -> Unit, modifier: Modifier = Modifier) {
    Surface(onClick = onClick, shape = MaterialTheme.shapes.medium, color = MaterialTheme.colorScheme.surfaceContainer,
        contentColor = MaterialTheme.colorScheme.onSurface, modifier = modifier.heightIn(min = 72.dp)) {
        Column(Modifier.padding(10.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
            Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(26.dp))
            Spacer(Modifier.size(6.dp))
            Text(label, style = MaterialTheme.typography.labelLarge, maxLines = 1)
        }
    }
}

/** 2-4 short options side by side; tapping one picks it. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun Choices(options: List<String>, selected: Int, onPick: (Int) -> Unit, modifier: Modifier = Modifier) {
    SingleChoiceSegmentedButtonRow(modifier.fillMaxWidth()) {
        options.forEachIndexed { i, label ->
            SegmentedButton(
                selected = i == selected, onClick = { onPick(i) },
                shape = SegmentedButtonDefaults.itemShape(index = i, count = options.size),
                icon = {}, // no tick: the colour shows the choice, and the labels keep their room
                colors = SegmentedButtonDefaults.colors(activeContainerColor = MaterialTheme.colorScheme.primary,
                    activeContentColor = MaterialTheme.colorScheme.onPrimary, inactiveContainerColor = MaterialTheme.colorScheme.surfaceContainer,
                    activeBorderColor = MaterialTheme.colorScheme.primary, inactiveBorderColor = MaterialTheme.colorScheme.outlineVariant),
            ) { Text(label, maxLines = 1, fontSize = 13.sp) }
        }
    }
}

/** A setting's help: under the setting it explains. */
@Composable
fun Help(text: String) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(top = 6.dp))
}

/** An on/off setting: the whole row toggles it. */
@Composable
fun SwitchRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit, help: String? = null) {
    Column(Modifier.fillMaxWidth().clickable { onChange(!checked) }.padding(vertical = 12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(label, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
            Switch(checked = checked, onCheckedChange = onChange)
        }
        if (help != null) Help(help)
    }
}

/** A setting with 2-4 options shown at once: its label, the [Choices], its help. */
@Composable
fun ChoiceRow(label: String, options: List<String>, selected: Int, onPick: (Int) -> Unit, help: String? = null) {
    Column(Modifier.fillMaxWidth().padding(vertical = 12.dp)) {
        Text(label, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.padding(bottom = 8.dp))
        Choices(options, selected, onPick)
        if (help != null) Help(help)
    }
}

/** An icon in an amber-tinted square (Settings' sections). */
@Composable
fun IconBadge(icon: ImageVector) {
    Box(Modifier.size(40.dp).clip(MaterialTheme.shapes.small).background(MaterialTheme.colorScheme.primaryContainer),
        contentAlignment = Alignment.Center) {
        Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(22.dp))
    }
}

/** A row that opens a page: an icon, its title, what it is set to now, and a chevron. */
@Composable
fun NavRow(icon: ImageVector?, title: String, summary: String?, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clickable(onClick = onClick).heightIn(min = 56.dp).padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically) {
        if (icon != null) {
            IconBadge(icon)
            Spacer(Modifier.width(14.dp))
        }
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            if (!summary.isNullOrEmpty()) {
                Text(summary, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null, tint = MaterialTheme.colorScheme.outline)
    }
    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
}

/** One line of an [ActionSheet]. [danger]: red, and set apart from the others. */
class SheetItem(val icon: ImageVector, val label: String, val danger: Boolean = false, val onClick: () -> Unit)

/** A panel from the bottom with icon + label lines; the danger lines come last, after a divider. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ActionSheet(title: String, items: List<SheetItem>, onClose: () -> Unit) {
    ModalBottomSheet(onDismissRequest = onClose, sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
        containerColor = MaterialTheme.colorScheme.surfaceContainerLow) {
        Column(Modifier.navigationBarsPadding().padding(bottom = 12.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium, maxLines = 2, overflow = TextOverflow.Ellipsis,
                modifier = Modifier.padding(horizontal = 24.dp, vertical = 8.dp))
            val (safe, danger) = items.partition { !it.danger }
            for (item in safe) SheetLine(item)
            if (danger.isNotEmpty()) {
                HorizontalDivider(Modifier.padding(vertical = 4.dp, horizontal = 24.dp), color = MaterialTheme.colorScheme.outlineVariant)
                for (item in danger) SheetLine(item)
            }
        }
    }
}

@Composable
private fun SheetLine(item: SheetItem) {
    val color = if (item.danger) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface
    Row(Modifier.fillMaxWidth().clickable(onClick = item.onClick).heightIn(min = 52.dp).padding(horizontal = 24.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Icon(item.icon, contentDescription = null, tint = if (item.danger) color else MaterialTheme.colorScheme.primary)
        Spacer(Modifier.width(16.dp))
        Text(item.label, color = color, style = MaterialTheme.typography.bodyLarge)
    }
}

/** A card's look: the theme's container colour, or the highlighted one with its border. */
@Composable
fun cardBorder(highlight: Boolean): BorderStroke? = if (highlight) BorderStroke(1.dp, ContinueBorder) else null
```

- [x] **Step 2: Build and run today's tests**

```bash
bash tools/dev.sh apk 2>&1 | grep -E "^e: |^w: |BUILD"
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: |BUILD"
```

Expected: `BUILD SUCCESSFUL` twice, no `e:` / `w:` lines; Kotlin `84 0`.

- [x] **Step 3: Commit**

```bash
git add app/build.gradle.kts app/src/main/java/com/tengen/gbaemu/AppTheme.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-8a-app-look.md
git commit -m "feat(app): the Cartridge grey theme and its reusable pieces (AppTheme)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: The Continue card's game

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Library.kt` (`Library.continueAndRest(rows)`: the most recently played game — largest `lastPlayed` > 0 — or null, and the rest in the list's order)
- Test: `app/src/test/java/com/tengen/gbaemu/LibraryTest.kt` (+2: `theContinueCardIsTheMostRecentlyPlayedGame`, `noContinueCardWhenNothingWasPlayed`)

- [x] **Step 1: Write the failing tests**

Replace `app/src/test/java/com/tengen/gbaemu/LibraryTest.kt`:

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

    @Test
    fun theContinueCardIsTheMostRecentlyPlayedGame() {
        val a = GameEntry("a", "u", "A", lastPlayed = 100)
        val b = GameEntry("b", "u", "B", lastPlayed = 300)
        val c = GameEntry("c", "u", "C")
        val (last, rest) = Library.continueAndRest(listOf(b, a, c))
        assertEquals(b, last)
        assertEquals(listOf(a, c), rest) // the rest in the list's order, without it
    }

    @Test
    fun noContinueCardWhenNothingWasPlayed() {
        val rows = listOf(GameEntry("a", "u", "A"), GameEntry("b", "u", "B"))
        val (last, rest) = Library.continueAndRest(rows)
        assertNull(last)
        assertEquals(rows, rest)
    }
}
```

- [x] **Step 2: Run them to see them fail**

```bash
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: " | head -2
```

Expected: FAIL to compile — `... 'ERROR CLASS: Unresolved name: continueAndRest' ...` (the destructuring of the result).

- [x] **Step 3: Write the code**

Replace `app/src/main/java/com/tengen/gbaemu/Library.kt`:

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

        /** The game list's Continue card (the most recently played game, or none) and the rest, in [rows]' order. */
        fun continueAndRest(rows: List<GameEntry>): Pair<GameEntry?, List<GameEntry>> {
            val last = rows.filter { it.lastPlayed > 0 }.maxByOrNull { it.lastPlayed }
            return last to rows.filter { it !== last }
        }

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
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: |BUILD"
```

Expected: `BUILD SUCCESSFUL`; Kotlin `86 0`.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Library.kt app/src/test/java/com/tengen/gbaemu/LibraryTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-8a-app-look.md
git commit -m "feat(app): the game list's Continue card - the most recently played game" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: The game list, the game sheet, the pause menu, slots and cheats

**Files:**
- Create: `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt` (the pause menu and the game list; Task 4 adds the Settings test)
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt` (first start; the grid with the Continue card — picture left and text right on screens over 560 dp wide —, a ⋮ on every card; the game sheet; the pause menu with Resume, four tiles, the speed as choice buttons, Restart / Quit, two columns sideways; slots — "Empty" once —; cheats; red for Replace / Delete / Restart / Remove; the sun icon; the speed label), `app/src/main/java/com/tengen/gbaemu/MainActivity.kt` (`AppTheme` at the root; `GameMenu` gets the speed as a number and sets it directly — `speedLabel` and `nextFastSpeed` go)

- [x] **Step 1: Write the failing tests**

Create `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt`:

```kotlin
package com.tengen.gbaemu

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

/** The screens of milestone 8a's new look: what they show and what tapping them does. */
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

- [x] **Step 2: Run them to see them fail**

```bash
bash tools/dev.sh androidtest 2>&1 | grep -E "^e: " | head -3
```

Expected: FAIL to compile — a type mismatch at `GameMenu("game C", 4, onFastSpeed = { picked = it }` (today's `GameMenu` takes the speed as a label and a no-argument callback).

- [x] **Step 3: Write the code**

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
               layouts: LayoutStore.Layouts = LayoutStore.Layouts(), showSpeed: Boolean = false) {
    BoxWithConstraints(Modifier.fillMaxSize().background(Color.Black)) {
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
                        onBack = { settingsPage = if (page == SettingsPage.MAIN) null else SettingsPage.MAIN },
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
                        onCancel = ::closeLayoutEditor)
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
                    settingsPage = if (settingsPage == SettingsPage.MAIN) null else SettingsPage.MAIN
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
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: |BUILD"
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
```

Expected: Kotlin `86 0`; `Starting 55 tests on Pixel_9_API_36_1(AVD)`, `tests="55" failures="0"` (53 + 2).

- [x] **Step 5: Commit**

```bash
git add app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-8a-app-look.md
git commit -m "feat(app): the game grid, the game sheet, the new pause menu, slots and cheats" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 4: Settings and the other screens

**Files:**
- Modify: `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt` (+ the Settings test), `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt` (sections with icons; switches and choice buttons instead of pick dialogs — short labels Fit · Whole steps · Stretch and 1 min · 2½ min · 5 min —; help under each setting; "and" for "&"; the key table, key wait, BIOS and game settings with the new look; the game settings' clock as choice buttons, its save type keeps the pick list), `app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt` (the theme's surface, "·" for "—"), `app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt` (the editor's outline amber, the picture block grey), `app/src/main/java/com/tengen/gbaemu/TouchLayout.kt` (the editor's names without symbols: "Rewind", "Fast-forward"), `app/src/main/java/com/tengen/gbaemu/MotionInput.kt` ("the light slider")

- [x] **Step 1: Write the failing test**

Replace `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt`:

```kotlin
package com.tengen.gbaemu

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

/** The screens of milestone 8a's new look: what they show and what tapping them does. */
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
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; grep -h -B1 -A1 "<failure" app/build/outputs/androidTest-results/connected/debug/*.xml | head -4
```

Expected: `tests="56" failures="1"` — `aSettingsSwitchAndChoiceChangeTheirSetting`, `expected:<true> but was:<false>` (today a tap on "Show speed" opens a pick dialog).

- [x] **Step 3: Write the code**

Replace `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.FastForward
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.Memory
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
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp

/** The Settings screen's pages: the sections, then one page each. */
enum class SettingsPage(val title: String) {
    MAIN("Settings"), PICTURE("Picture"), SOUND("Sound"), SPEED("Fast-forward and rewind"), KEYS("Keyboard and controller"),
    TOUCH("Touch buttons"), BIOS("BIOS"),
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
                        NavRow(Icons.Filled.TouchApp, "Touch buttons", "${Settings.LOOK_NAMES[s.touchLook]} · Buzz ${if (s.buzz) "on" else "off"}") { onPage(SettingsPage.TOUCH) }
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
                        ChoiceRow("Sideways, over the picture", Settings.LOOK_NAMES, s.touchLook, { onChange(s.copy(touchLook = it)) },
                            help = "How strongly the buttons show over the game when the phone is sideways.")
                        SwitchRow("Buzz", s.buzz, { onChange(s.copy(buzz = it)) },
                            help = "Follows the phone's own touch-vibration setting.")
                        Spacer(Modifier.height(8.dp))
                        NavRow(Icons.Filled.StayCurrentPortrait, "Edit upright layout", "Move and size every button and the game picture") { onEditLayout(false) }
                        NavRow(Icons.Filled.StayCurrentLandscape, "Edit sideways layout", "Move and size every button and the game picture") { onEditLayout(true) }
                    }
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
                 onSave: (Map<LayoutItem, Place>) -> Unit, onCancel: () -> Unit) {
    var edits by remember { mutableStateOf(start) }
    var selected by remember { mutableStateOf<LayoutItem?>(null) }
    BackHandler { onCancel() }
    BoxWithConstraints(Modifier.fillMaxSize().background(Color.Black)) {
        val turnedRight = (maxWidth > maxHeight) == sideways // the screen has turned to the edited orientation
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { TouchControlsView(it).apply { editing = true } },
            update = { v ->
                v.sideways = sideways
                v.edits = edits
                v.look = look
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
 * vibration tick (Android's own touch feedback, so the phone's setting applies) unless [buzz] is off. Buttons over the
 * game picture are see-through ([look]: Settings.LOOK_*), the others solid. While [hidden] (a keyboard or gamepad is in
 * use) nothing is drawn; the next touch shows the buttons again.
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

    private val dpadShape = Path() // the cross as one shape, so see-through arms do not overlap

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) = relayout()

    private fun relayout() {
        val l = if (width > 0 && height > 0) TouchLayout(width.toFloat(), height.toFloat(), density, sideways, edits) else null
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
        // On black: solid grey (as upright since 4c). Over the game picture: see-through white (Faint / Normal / Strong).
        val (idleA, downA, textA) = when (look) {
            Settings.LOOK_FAINT -> Triple(0x33, 0x66, 0x88)
            Settings.LOOK_STRONG -> Triple(0x99, 0xCC, 0xFF)
            else -> Triple(0x55, 0x99, 0xDD)
        }
        fun colours(over: Boolean) = if (over) {
            Triple((idleA shl 24) or 0xFFFFFF, (downA shl 24) or 0xFFFFFF, (textA shl 24) or 0xFFFFFF)
        } else {
            Triple(0xFFB4B4B4.toInt(), 0xFF6E6E6E.toInt(), 0xFF222222.toInt())
        }

        // D-pad: the cross, then each pressed arm (outside the middle) again, darker (see-through: lighter over it).
        val (dIdle, dDown, _) = colours(layout.dpadOverPicture)
        fill.color = dIdle
        canvas.drawPath(dpadShape, fill)
        val x = layout.dpadX
        val y = layout.dpadY
        val r = layout.dpadRadius
        val a = r * ARM
        fill.color = if (layout.dpadOverPicture) dIdle else dDown
        if (mask and KeyMap.UP != 0) canvas.drawRoundRect(x - a, y - r, x + a, y - a, a / 3, a / 3, fill)
        if (mask and KeyMap.DOWN != 0) canvas.drawRoundRect(x - a, y + a, x + a, y + r, a / 3, a / 3, fill)
        if (mask and KeyMap.LEFT != 0) canvas.drawRoundRect(x - r, y - a, x - a, y + a, a / 3, a / 3, fill)
        if (mask and KeyMap.RIGHT != 0) canvas.drawRoundRect(x + a, y - a, x + r, y + a, a / 3, a / 3, fill)

        for (b in layout.buttons) {
            val (idle, down, ink) = colours(layout.overPicture(b))
            fill.color = when {
                mask and b.bit != 0 -> down
                b.bit == KeyMap.FAST && fastOn -> LIT
                else -> idle
            }
            if (b.round) {
                canvas.drawCircle(b.cx, b.cy, (b.right - b.left) / 2, fill)
            } else {
                val corner = (b.bottom - b.top) / 2
                canvas.drawRoundRect(b.left, b.top, b.right, b.bottom, corner, corner, fill)
            }
            text.color = ink
            text.textSize = b.scale * when {
                b.round -> 22 * density
                b.bit == KeyMap.FAST || b.bit == KeyMap.REWIND -> 20 * density // symbols, not words
                else -> 13 * density
            }
            canvas.drawText(b.label, b.cx, b.cy - (text.ascent() + text.descent()) / 2, text)
        }
        if (editing) selected?.let { item ->
            val box = layout.box(item)
            val pad = 4 * density
            canvas.drawRect(box.left - pad, box.top - pad, box.right + pad, box.bottom + pad, outline)
        }
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
        const val LIT = 0xFFFFD54A.toInt() // ⏩ while fast mode is switched on
        const val PICTURE_BLOCK = 0xFF2A2C31.toInt()
    }
}
```

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
 * Pure maths (no Android classes), so it is tested on the PC.
 */
class TouchLayout(val width: Float, val height: Float, private val density: Float, val sideways: Boolean,
                  val edits: Map<LayoutItem, Place> = emptyMap()) {
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
        TouchLayout(width, height, density, sideways, edits + (item to place.copy(scale = clampScale(place.scale))))

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

    /** Over the game picture: drawn see-through; on black: solid. */
    fun overPicture(b: Button) = b.box.overlaps(picture)
    val dpadOverPicture get() = box(LayoutItem.DPAD).overlaps(picture)

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

Replace `app/src/main/java/com/tengen/gbaemu/MotionInput.kt`:

```kotlin
package com.tengen.gbaemu

import android.app.Activity
import android.content.pm.ApplicationInfo
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.SystemClock
import android.util.Log
import androidx.core.content.ContextCompat

/**
 * The cartridge's sensors from the phone: the accelerometer (tilt), the gyroscope (turn) and the light sensor (solar),
 * each only when the cartridge has it ([hardware] = NativeBridge.HW_* bits); the I/J/K/L and Q/E keys override their
 * axis while held; [manualLight] (the ☀ slider) overrides the light sensor. Every change goes to the core at once.
 */
class MotionInput(private val activity: Activity, private val hardware: Int, private val onMissing: (String) -> Unit) :
    SensorEventListener {
    private val sensors = activity.getSystemService(SensorManager::class.java)
    private val debug = (activity.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
    private var ax = 0f
    private var ay = 0f
    private var wz = 0f
    private var lux = 0f
    private var keys = 0
    private var missingTold = false
    private var lastLog = ""
    private var lastLogMs = 0L
    private var started = false

    /** null = Auto (the light sensor); 0..255 = set by hand. */
    var manualLight: Int? = null
        set(value) {
            field = value
            push()
        }

    fun start() {
        if (started) return
        started = true
        val wanted = buildList {
            if (hardware and NativeBridge.HW_TILT != 0) add(Sensor.TYPE_ACCELEROMETER to "motion (tilt)")
            if (hardware and NativeBridge.HW_GYRO != 0) add(Sensor.TYPE_GYROSCOPE to "gyroscope (turn)")
            if (hardware and NativeBridge.HW_SOLAR != 0) add(Sensor.TYPE_LIGHT to "light")
        }
        val missing = mutableListOf<String>()
        for ((type, name) in wanted) {
            val s = sensors?.getDefaultSensor(type)
            if (s == null) missing += name else sensors.registerListener(this, s, SensorManager.SENSOR_DELAY_GAME)
        }
        if (missing.isNotEmpty() && !missingTold) {
            missingTold = true
            onMissing("This phone has no ${missing.joinToString(" or ")} sensor: use the keys or the light slider.")
        }
        push()
    }

    fun stop() {
        if (!started) return
        started = false
        sensors?.unregisterListener(this)
    }

    /** A motion key (MotionMath.KEY_*) went down or up. */
    fun setKey(bit: Int, down: Boolean) {
        keys = if (down) keys or bit else keys and bit.inv()
        push()
    }

    override fun onSensorChanged(event: SensorEvent) {
        when (event.sensor.type) {
            Sensor.TYPE_ACCELEROMETER -> {
                ax = event.values[0]
                ay = event.values[1]
            }
            Sensor.TYPE_GYROSCOPE -> wz = event.values[2]
            Sensor.TYPE_LIGHT -> lux = event.values[0]
        }
        push()
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}

    private fun push() {
        val rotation = ContextCompat.getDisplayOrDefault(activity).rotation
        val (sx, sy) = MotionMath.tilt(ax, ay, rotation)
        val x = MotionMath.withKeys(sx, keys and MotionMath.KEY_J != 0, keys and MotionMath.KEY_L != 0, MotionMath.KEY_TILT)
        val y = MotionMath.withKeys(sy, keys and MotionMath.KEY_I != 0, keys and MotionMath.KEY_K != 0, MotionMath.KEY_TILT)
        val turn = MotionMath.withKeys(MotionMath.turn(wz), keys and MotionMath.KEY_Q != 0, keys and MotionMath.KEY_E != 0,
            MotionMath.KEY_TURN)
        val light = manualLight ?: MotionMath.light(lux)
        NativeBridge.setMotion(x, y, turn)
        NativeBridge.setLight(light)
        if (debug) { // for checking with the phone emulator's virtual sensors: at most once a second, on changes
            val line = "motion: tilt $x $y turn $turn light $light${if (manualLight != null) " (manual)" else ""}"
            val now = SystemClock.elapsedRealtime()
            if (line != lastLog && now - lastLogMs >= 1000) {
                Log.i("gbaemu", line)
                lastLog = line
                lastLogMs = now
            }
        }
    }
}
```

- [x] **Step 4: The tests on the emulator, and today's**

```bash
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: |BUILD"
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
```

Expected: Kotlin `86 0`; `tests="56" failures="0"`.

- [x] **Step 5: Commit**

```bash
git add app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt app/src/main/java/com/tengen/gbaemu/TouchLayout.kt app/src/main/java/com/tengen/gbaemu/MotionInput.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-8a-app-look.md
git commit -m "feat(app): Settings with switches and choice buttons; the editor and messages in the new look" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 5: The wording test

**Files:**
- Create: `app/src/test/java/com/tengen/gbaemu/UiTextTest.kt` (reads every string literal of `app/src/main/java` — plain and raw strings, templates' code read again, comments skipped — and fails on "…", "...", "&", ▶ ⋮ ☀ ⏩ ⟲, naming file and line; allowed: the touch buttons' drawn ⏩ ⟲ in `TouchLayout.kt` / `TouchControlsView.kt`, 8b's; the reader's own test)

(It passes at once: Tasks 3-4 removed every such label. In the prototype a planted "Settings…" in the pause menu made it fail, naming `Screens.kt` and the line. The reader is written without backslash escapes — character codes — because shell quoting mangled them in the prototype.)

- [x] **Step 1: Write the test**

Create `app/src/test/java/com/tengen/gbaemu/UiTextTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The words the app shows (milestone 8a): no "…" or "...", no "&", and no text symbols standing in for icons. Reads
 * every string literal in the app's Kotlin sources (comments are skipped), so a label can't slip back in.
 */
class UiTextTest {
    /** A string literal: its file, line and text (a template's code parts left out). */
    data class Literal(val file: String, val line: Int, val text: String)

    @Test
    fun noEllipsisAmpersandOrSymbolIconsInTheAppsText() {
        val bad = literals().flatMap { lit ->
            FORBIDDEN.filter { it in lit.text && (lit.file to it) !in ALLOWED }.map { "${lit.file}:${lit.line} \"${lit.text}\" has $it" }
        }
        assertEquals("", bad.joinToString("\n"))
    }

    @Test
    fun theReaderFindsLiteralsAndSkipsComments() {
        val q = QUOTE.toString()
        val src = "val a = ${q}one$q // ${q}not this…$q\n" +
            "/* ${q}nor this…$q */ val b = ${q}t ${DOLLAR}{if (c) ${q}in…$q else ${q}x$q} end$q\n" +
            "val r = $q$q${q}raw$q$q$q"
        val found = scan("x.kt", src)
        assertEquals(listOf("one", "t ", "in…", "x", " end", "raw"), found.map { it.text })
        assertEquals(listOf(1, 2, 2, 2, 2, 3), found.map { it.line })
    }

    private fun literals(): List<Literal> {
        val root = File("src/main/java")
        assertTrue("run from the app module (src/main/java not found)", root.isDirectory)
        return root.walkTopDown().filter { it.extension == "kt" }.flatMap { scan(it.name, it.readText()) }.toList()
    }

    companion object {
        val FORBIDDEN = listOf("…", "...", "&", "▶", "⋮", "☀", "⏩", "⟲")

        /** The touch buttons draw these on themselves (their look is milestone 8b's). */
        val ALLOWED = setOf("TouchLayout.kt" to "⏩", "TouchLayout.kt" to "⟲", "TouchControlsView.kt" to "⏩", "TouchControlsView.kt" to "⟲")

        // Written as codes: the reader is about these very characters.
        val NL = 10.toChar()
        val QUOTE = 34.toChar()
        val DOLLAR = 36.toChar()
        val APOS = 39.toChar()
        val BACKSLASH = 92.toChar()
        val TRIPLE = "$QUOTE$QUOTE$QUOTE"

        /** Every string literal's text in Kotlin [src]: plain and raw strings, templates' code read again for nested ones. */
        fun scan(file: String, src: String): List<Literal> = Scanner(file, src).run { code(inTemplate = false); out }
    }

    private class Scanner(val file: String, val src: String) {
        val out = mutableListOf<Literal>()
        var i = 0
        var line = 1

        /** Code, up to the unmatched closing brace that ends a template's code when [inTemplate]. */
        fun code(inTemplate: Boolean) {
            var depth = 0
            while (i < src.length) {
                val c = src[i]
                when {
                    c == NL -> { line++; i++ }
                    src.startsWith("//", i) -> while (i < src.length && src[i] != NL) i++
                    src.startsWith("/*", i) -> {
                        val end = src.indexOf("*/", i + 2).let { if (it < 0) src.length else it + 2 }
                        line += src.substring(i, end).count { it == NL }
                        i = end
                    }
                    c == APOS -> charLiteral()
                    c == QUOTE -> string()
                    c == '{' -> { depth++; i++ }
                    c == '}' -> {
                        i++
                        if (depth == 0 && inTemplate) return
                        depth--
                    }
                    else -> i++
                }
            }
        }

        fun charLiteral() {
            i++ // the opening quote
            i += if (src[i] == BACKSLASH) 2 else 1
            while (i < src.length && src[i] != APOS) i++ // the rest of a unicode escape
            i++
        }

        fun string() {
            val raw = src.startsWith(TRIPLE, i)
            i += if (raw) 3 else 1
            val text = StringBuilder()
            var startLine = line
            fun flush() {
                if (text.isNotEmpty()) out += Literal(file, startLine, text.toString())
                text.clear()
            }
            while (i < src.length) {
                val c = src[i]
                if (raw && src.startsWith(TRIPLE, i)) { i += 3; break }
                if (!raw && c == QUOTE) { i++; break }
                if (!raw && c == BACKSLASH) { text.append(src[i + 1]); i += 2; continue }
                if (c == DOLLAR && i + 1 < src.length && src[i + 1] == '{') {
                    flush()
                    i += 2
                    code(inTemplate = true)
                    startLine = line
                    continue
                }
                if (c == NL) line++
                text.append(c)
                i++
            }
            flush()
        }
    }
}
```

- [x] **Step 2: Run it, and every app check**

```bash
bash tools/dev.sh jvmtest 2>&1 | grep -E "^e: |BUILD"
echo "warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c '^w: ')"
git diff proto-8a --stat -- app
```

Expected: `BUILD SUCCESSFUL`, Kotlin `88 0`; `warnings: 0`; no output from the diff (the app is the prototype's, byte for byte).

- [x] **Step 3: Commit**

```bash
git add app/src/test/java/com/tengen/gbaemu/UiTextTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-8a-app-look.md
git commit -m "test(app): no ellipsis, ampersand or symbol icons in the app's text" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6: User check and milestone 8a exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test 2>&1 | grep -E "tests passed|tests failed"
bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest && bash tools/dev.sh install
```

Expected: CTest `100% tests passed, 0 tests failed out of 120` (the core unchanged); Kotlin 88; on-device 56 on the emulator; the app installed on the emulator for the user's check.

- [x] **Step 2: The user's check** (on the phone emulator, or their phone if they ask)

The game list (Continue card, the grid, a card's ⋮ sheet), the pause menu (Back in a game: Resume, the tiles, a speed, Restart asks, Quit), Settings (switches and choice buttons take effect: e.g. Smooth pixels, Show speed), save and load a state, a cheat, upright and sideways — easier and nicer than before, no "…" anywhere, everything works as before.

- [x] **Step 3: Docs**

`docs/PROGRESS.md` (8a ✅ with the user's check; Next = brainstorm 8b, the console skins); spec status → `Implemented — milestone 8a complete <date> (tag milestone-8a)`; parent spec status line and §10 row 8 → 8a ✅; `CLAUDE.md` key files → 8b needs brainstorming; tick every box here.

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`; record the merge in PROGRESS.md. The branch `milestone-8` stays for 8b unless the user says otherwise; delete the prototype worktree and branch (`rm -rf <scratchpad>/p8a` in Git Bash, `git worktree prune`, `git branch -D proto-8a`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 8a complete - the app's new look" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-8a
```
