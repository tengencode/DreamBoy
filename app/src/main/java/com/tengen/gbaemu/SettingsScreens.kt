package com.tengen.gbaemu

import android.graphics.Bitmap
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
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
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Archive
import androidx.compose.material.icons.filled.EmojiEvents
import androidx.compose.material.icons.filled.FastForward
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.SportsEsports
import androidx.compose.material.icons.filled.StayCurrentLandscape
import androidx.compose.material.icons.filled.StayCurrentPortrait
import androidx.compose.material.icons.filled.TouchApp
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.autofill.ContentType
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.semantics.contentType
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp

/** The Settings screen's pages: the sections, then one page each; the Skin page (8b) inside Touch buttons. */
enum class SettingsPage(val title: String) {
    MAIN("Settings"), PICTURE("Picture"), SOUND("Sound"), SPEED("Fast-forward and rewind"), KEYS("Keyboard and controller"),
    TOUCH("Touch buttons"), BIOS("BIOS"), SKIN("Skin"), BACKUP("Backup"), ACHIEVEMENTS("RetroAchievements"),
    ABOUT("About DreamBoy"), LICENCES("Open-source licences"), LICENCE("Licence"), PRIVACY("Privacy policy");

    /** Where Back goes: the section a page is in; from the main page out of Settings (null). */
    val up: SettingsPage? get() = when (this) {
        MAIN -> null
        SKIN -> TOUCH
        LICENCES, PRIVACY -> ABOUT
        LICENCE -> LICENCES
        else -> MAIN
    }
}

/** A pick list waiting to be shown: its title, the choices, the one chosen now, and what picking does. */
private class Choice(val title: String, val options: List<String>, val selected: Int, val onPick: (Int) -> Unit)

/** The choice buttons' short labels, where the full names (the main page's summaries) don't fit a phone-width row. */
val SIZE_SHORT = listOf("Fit", "Whole steps", "Stretch")
val LENGTH_SHORT = listOf("1 min", "2½ min", "5 min")

/** An imported Delta skin as the Skin page shows it (9): its number, name, tile picture and which directions it covers. */
data class ImportedSkin(val number: Int, val name: String, val thumb: Bitmap?, val upright: Boolean, val sideways: Boolean)

/** What the Backup page shows (11): the last backup (0 = none), automatic backup and its folder, Undo, and whether a game is open. */
data class BackupStatus(val lastBackup: Long = 0, val auto: Boolean = false, val folderName: String? = null,
                        val canUndo: Boolean = false, val inGame: Boolean = false)

/** The Backup row's summary: "Last backup today 14:02", "No backup yet", with " · automatic" when it is on. */
fun backupSummary(status: BackupStatus): String =
    (if (status.lastBackup > 0) "Last backup " + playedText(status.lastBackup).replaceFirstChar { it.lowercase() } else "No backup yet") +
        if (status.auto) " · automatic" else ""

/** The RetroAchievements sign-in as the Settings screen shows it (14). */
data class RaUi(val signIn: Achievements.SignIn = Achievements.SignIn.OUT, val name: String? = null, val error: String? = null)

/** The main page's line for RetroAchievements. */
fun raSummary(ra: RaUi, hardcore: Boolean): String = when (ra.signIn) {
    Achievements.SignIn.IN -> "Signed in as ${ra.name}" + if (hardcore) " · Hardcore" else ""
    Achievements.SignIn.BUSY -> "Signing in"
    Achievements.SignIn.AGAIN -> "Sign in again"
    Achievements.SignIn.OUT -> "Not signed in"
}

/**
 * Settings (from the library's gear and the in-game menu). [keysChanged] changes whenever the key bindings do, so
 * the key table is read again. Every change goes to [onChange] at once (saved and applied by the caller).
 * [importedSkins]: the imported Delta skins (9); the chosen one ([Settings.skinImport]) sets the layout of the
 * directions it covers.
 */
@Composable
fun SettingsScreen(page: SettingsPage, settings: Settings, keys: KeyBindings, keysChanged: Int, biosName: String,
                   onPage: (SettingsPage) -> Unit, onBack: () -> Unit, onChange: (Settings) -> Unit,
                   onPickKey: (bit: Int, device: Int) -> Unit, onResetKeys: () -> Unit, onChooseBios: () -> Unit,
                   onBuiltInBios: () -> Unit, onEditLayout: (sideways: Boolean, gameBoy: Boolean) -> Unit = { _, _ -> },
                   importedSkins: List<ImportedSkin> = emptyList(), onImportSkin: () -> Unit = {},
                   onRemoveSkin: (Int) -> Unit = {}, backup: BackupStatus = BackupStatus(), onBackUp: () -> Unit = {},
                   onRestore: () -> Unit = {}, onUndoRestore: () -> Unit = {}, onAutoBackup: (Boolean) -> Unit = {},
                   onChangeFolder: () -> Unit = {}, ra: RaUi = RaUi(), onRaSignIn: (String, String) -> Unit = { _, _ -> },
                   onRaSignOut: () -> Unit = {}, version: String = "", onOpenGithub: () -> Unit = {}) {
    val s = settings
    var licence by rememberSaveable { mutableIntStateOf(0) } // the one Settings → About → licences shows
    val chosen = importedSkins.firstOrNull { it.number == s.skinImport } // an imported skin in use, or null
    val skinName = chosen?.name ?: Skins.at(s.skin).name
    Surface(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().systemBarsPadding().imePadding()) { // the sign-in fields stay above the keyboard
            ScreenHeader(page.title, onBack = onBack, subtitle = if (page == SettingsPage.LICENCE) Licences.SHIPPED[licence].part else null)
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp)
                .padding(bottom = 24.dp).widthIn(max = 640.dp)) {
                when (page) {
                    SettingsPage.MAIN -> {
                        Group {
                        NavRow(Icons.Filled.Image, "Picture", "${if (s.smooth) "Smooth" else "Sharp"} · ${Settings.SIZE_NAMES[s.size]}") { onPage(SettingsPage.PICTURE) }
                        NavRow(Icons.AutoMirrored.Filled.VolumeUp, "Sound", if (s.sound) "On · ${Settings.DELAY_NAMES[s.delay]} delay" else "Off") { onPage(SettingsPage.SOUND) }
                        NavRow(Icons.Filled.FastForward, "Fast-forward and rewind", Settings.speedName(s.fastSpeed) + " · " +
                            if (s.rewind) "Rewind ${Settings.LENGTH_NAMES[s.rewindLength].lowercase()}" else "Rewind off") { onPage(SettingsPage.SPEED) }
                        NavRow(Icons.Filled.SportsEsports, "Keyboard and controller", "Which key does what") { onPage(SettingsPage.KEYS) }
                        NavRow(Icons.Filled.TouchApp, "Touch buttons", "$skinName · ${Settings.LOOK_NAMES[s.touchLook]} · Buzz ${if (s.buzz) "on" else "off"}") { onPage(SettingsPage.TOUCH) }
                        NavRow(Icons.Filled.Memory, "BIOS", biosName) { onPage(SettingsPage.BIOS) }
                        NavRow(Icons.Filled.Archive, "Backup", backupSummary(backup), last = true) { onPage(SettingsPage.BACKUP) }
                        }
                        Group {
                            NavRow(Icons.Filled.EmojiEvents, "RetroAchievements", raSummary(ra, s.hardcore), last = true) {
                                onPage(SettingsPage.ACHIEVEMENTS)
                            }
                        }
                        Group {
                            SwitchRow("Download box art", s.boxArt, { onChange(s.copy(boxArt = it)) },
                                help = "Pictures of game boxes from libretro, for the game list. Uses the internet.")
                        }
                        Group {
                            NavRow(Icons.Filled.Info, "About DreamBoy", "Version $version", last = true) { onPage(SettingsPage.ABOUT) }
                        }
                    }
                    SettingsPage.PICTURE -> Group {
                        ChoiceRow("Pixels", listOf("Sharp", "Smooth"), if (s.smooth) 1 else 0, { onChange(s.copy(smooth = it == 1)) },
                            help = "Sharp keeps every GBA pixel crisp; Smooth softens the edges.")
                        ChoiceRow("Size", SIZE_SHORT, s.size, { onChange(s.copy(size = it)) },
                            help = "Whole steps: every GBA pixel the same size (crisper, a little smaller). Sideways, the size matters most.")
                        SwitchRow("Show speed", s.showSpeed, { onChange(s.copy(showSpeed = it)) },
                            help = "Frames per second at the top of the game, and how many times real speed (1.0× = normal).")
                    }
                    SettingsPage.SOUND -> Group {
                        SwitchRow("Sound", s.sound, { onChange(s.copy(sound = it)) })
                        ChoiceRow("Delay", Settings.DELAY_NAMES, s.delay, { onChange(s.copy(delay = it)) },
                            help = "Low is the quickest. If the sound crackles on a busy phone, try Medium or High.")
                    }
                    SettingsPage.SPEED -> Group {
                        ChoiceRow("Fast-forward speed", Settings.SPEEDS.map(Settings::speedName),
                            Settings.SPEEDS.indexOf(s.fastSpeed).coerceAtLeast(0), { onChange(s.copy(fastSpeed = Settings.SPEEDS[it])) },
                            help = "Max runs as fast as the phone can, without sound.")
                        SwitchRow("Rewind", s.rewind, { onChange(s.copy(rewind = it)) })
                        ChoiceRow("Rewind length", LENGTH_SHORT, s.rewindLength, { onChange(s.copy(rewindLength = it)) },
                            help = "Uses up to 24, 64 or 128 MB. Rewind changes are used the next time a game opens.")
                    }
                    SettingsPage.KEYS -> KeyTable(keys, keysChanged, onPickKey, onResetKeys)
                    SettingsPage.TOUCH -> {
                        Group { NavRow(Icons.Filled.Palette, "Skin", skinName, last = true) { onPage(SettingsPage.SKIN) } }
                        Group {
                        ChoiceRow("Sideways, over the picture", Settings.LOOK_NAMES, s.touchLook, { onChange(s.copy(touchLook = it)) },
                            help = "How strongly the buttons show over the game when the phone is sideways.")
                        SwitchRow("Buzz", s.buzz, { onChange(s.copy(buzz = it)) },
                            help = "Follows the phone's own touch-vibration setting.")
                        }
                        Group {
                        val own = "This skin sets its own layout"
                        val edit = "Move and size every button and the game picture"
                        NavRow(Icons.Filled.StayCurrentPortrait, "Edit upright layout", if (chosen?.upright == true) own else edit,
                            enabled = chosen?.upright != true) { onEditLayout(false, false) }
                        NavRow(Icons.Filled.StayCurrentLandscape, "Edit sideways layout", if (chosen?.sideways == true) own else edit,
                            enabled = chosen?.sideways != true) { onEditLayout(true, false) }
                        // Game Boy and Game Boy Color games: another picture shape and no L and R, so their own layouts
                        // (an imported skin is a GBA skin: never in the way here).
                        val gb = "For Game Boy and Game Boy Color games"
                        NavRow(Icons.Filled.StayCurrentPortrait, "Edit Game Boy upright layout", gb) { onEditLayout(false, true) }
                        NavRow(Icons.Filled.StayCurrentLandscape, "Edit Game Boy sideways layout", gb, last = true) { onEditLayout(true, true) }
                        }
                    }
                    SettingsPage.SKIN -> {
                        SkinGrid(if (chosen != null) -1 else s.skin, importedSkins, chosen?.number,
                            onPick = { onChange(s.copy(skin = it, skinImport = 0)) },
                            onPickImported = { onChange(s.copy(skinImport = it)) }, onImport = onImportSkin, onRemove = onRemoveSkin)
                        Help("Import skins made for the Delta emulator (.deltaskin files). Press and hold an imported skin to remove it.")
                    }
                    SettingsPage.BACKUP -> {
                        Spacer(Modifier.height(8.dp))
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
                        Group {
                            SwitchRow("Automatic backup", backup.auto, onAutoBackup,
                                help = "After you quit a game, at most once a day, a backup goes into a folder you pick. The newest 5 are kept.")
                            if (backup.auto) NavRow(Icons.Filled.Folder, "Change folder", backup.folderName ?: "The picked folder", last = true) { onChangeFolder() }
                        }
                        Help("Backups stay where you put them. The app doesn't send them anywhere, and Android's own backup is off for this app.")
                    }
                    SettingsPage.BIOS -> {
                        Group {
                            Row(Modifier.fillMaxWidth().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                                Text("In use", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                                Text(biosName, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                        }
                        Spacer(Modifier.height(12.dp))
                        Button(onClick = onChooseBios, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("Choose BIOS file") }
                        if (s.biosFile) {
                            Spacer(Modifier.height(8.dp))
                            OutlinedButton(onClick = onBuiltInBios, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("Use built-in BIOS") }
                        }
                        Help("A dump of your own GBA's BIOS (16 KB). Games then start with the Nintendo logo. Used the next time a game opens; save states made with the other BIOS can't be loaded.")
                    }
                    SettingsPage.ACHIEVEMENTS -> RaPage(ra, s, onChange, onRaSignIn, onRaSignOut)
                    SettingsPage.ABOUT -> AboutPage(version, onOpenGithub, onPage)
                    SettingsPage.LICENCES -> LicencesPage { licence = it; onPage(SettingsPage.LICENCE) }
                    SettingsPage.LICENCE -> LicenceText(Licences.SHIPPED[licence])
                    SettingsPage.PRIVACY -> PrivacyPage()
                }
            }
        }
    }
}

/**
 * Settings → RetroAchievements (14): signed out, the sign-in panel (username, password with show / hide, the server's
 * message, Sign in turning into a spinner while it goes); signed in, the account and Sign out. The Hardcore switch on
 * both.
 */
@Composable
private fun RaPage(ra: RaUi, s: Settings, onChange: (Settings) -> Unit, onSignIn: (String, String) -> Unit, onSignOut: () -> Unit) {
    Spacer(Modifier.height(8.dp))
    if (ra.signIn == Achievements.SignIn.IN) {
        Group {
            Row(Modifier.fillMaxWidth().heightIn(min = 64.dp).padding(horizontal = 16.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Filled.EmojiEvents, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(24.dp))
                Spacer(Modifier.width(16.dp))
                Column(Modifier.weight(1f)) {
                    Text(ra.name.orEmpty(), style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Medium,
                        maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text("Signed in to RetroAchievements", style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        OutlinedButton(onClick = onSignOut, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("Sign out") }
        ra.error?.let { Help(it) }
    } else {
        var user by remember(ra.name) { mutableStateOf(ra.name.orEmpty()) }
        var password by remember { mutableStateOf("") }
        var showPassword by remember { mutableStateOf(false) }
        val busy = ra.signIn == Achievements.SignIn.BUSY
        val canSend = !busy && user.isNotBlank() && password.isNotEmpty()
        fun send() {
            if (!canSend) return
            onSignIn(user.trim(), password)
            password = ""
            showPassword = false
        }
        if (ra.signIn == Achievements.SignIn.AGAIN) {
            Text("Your sign-in stopped working. Sign in again.", color = MaterialTheme.colorScheme.error,
                style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(top = 4.dp, bottom = 6.dp))
        }
        Group {
            Column(Modifier.padding(16.dp)) {
                Text("Your retroachievements.org account. The password goes to RetroAchievements once and is never kept on this phone. Uses the internet.",
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(bottom = 12.dp))
                OutlinedTextField(user, { user = it }, label = { Text("Username") }, singleLine = true, enabled = !busy,
                    keyboardOptions = KeyboardOptions(autoCorrectEnabled = false, keyboardType = KeyboardType.Ascii, imeAction = ImeAction.Next),
                    modifier = Modifier.fillMaxWidth().semantics { contentType = ContentType.Username })
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(password, { password = it }, label = { Text("Password") }, singleLine = true, enabled = !busy,
                    isError = ra.error != null && password.isEmpty(),
                    visualTransformation = if (showPassword) VisualTransformation.None else PasswordVisualTransformation(),
                    trailingIcon = {
                        IconButton(onClick = { showPassword = !showPassword }, enabled = !busy) {
                            Icon(if (showPassword) Icons.Filled.VisibilityOff else Icons.Filled.Visibility,
                                contentDescription = if (showPassword) "Hide password" else "Show password")
                        }
                    },
                    keyboardOptions = KeyboardOptions(autoCorrectEnabled = false, keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
                    keyboardActions = KeyboardActions(onDone = { send() }),
                    modifier = Modifier.fillMaxWidth().semantics { contentType = ContentType.Password })
                ra.error?.let {
                    Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(top = 8.dp))
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        Button(onClick = ::send, enabled = canSend, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
            if (busy) {
                CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp, color = LocalContentColor.current)
                Spacer(Modifier.width(10.dp))
                Text("Signing in")
            } else Text("Sign in")
        }
    }
    Spacer(Modifier.height(12.dp))
    Group {
        SwitchRow("Hardcore mode", s.hardcore, { onChange(s.copy(hardcore = it)) },
            help = "Earn hardcore points. In games with achievements, Load state, rewind and cheats are off.")
        SwitchRow("Leaderboard notes on screen", s.leaderboardNotes, { onChange(s.copy(leaderboardNotes = it)) },
            help = "Shows when a leaderboard attempt starts or fails, and its live counter. Your score and rank always show. Leaderboards run only in Hardcore mode.")
    }
}

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
        border = chosenBorder(chosen),
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

@Composable
private fun SkinTile(skin: Skin, chosen: Boolean, onClick: () -> Unit, modifier: Modifier) {
    Surface(onClick = onClick, shape = MaterialTheme.shapes.medium, color = MaterialTheme.colorScheme.surfaceContainer,
        border = chosenBorder(chosen), modifier = modifier) {
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
    Group(Modifier.padding(top = 8.dp)) {
        Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 8.dp, top = 14.dp, bottom = 6.dp)) {
            for (h in listOf("GBA", "Keyboard", "Controller")) {
                Text(h, Modifier.weight(1f), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        rows.forEachIndexed { i, (b, kbd, pad) ->
            Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(b.button, Modifier.weight(1f), fontWeight = FontWeight.Medium)
                TextButton(onClick = { onPickKey(b.bit, KeyBindings.KEYBOARD) }, Modifier.weight(1f)) { Text(kbd) }
                TextButton(onClick = { onPickKey(b.bit, KeyBindings.CONTROLLER) }, Modifier.weight(1f)) { Text(pad) }
            }
            if (i < rows.lastIndex) HorizontalDivider(Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant)
        }
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
