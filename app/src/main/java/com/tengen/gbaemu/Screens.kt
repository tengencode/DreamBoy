package com.tengen.gbaemu

import android.content.Context
import android.graphics.Bitmap
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.AnimationVector1D
import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.sp
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.fillMaxHeight
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
import androidx.compose.material.icons.filled.CloudDownload
import androidx.compose.material.icons.filled.EmojiEvents
import androidx.compose.material.icons.filled.Leaderboard
import androidx.compose.material.icons.filled.FileDownload
import androidx.compose.material.icons.filled.FileUpload
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.HideImage
import androidx.compose.material.icons.filled.Image
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
import androidx.compose.material3.HorizontalDivider
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
import androidx.compose.ui.platform.LocalConfiguration
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

/** The very first start (no games folder yet) or a folder that can't be opened any more: an empty slot waiting. */
@Composable
fun FirstStartScreen(folderLost: Boolean, message: String?, onChooseFolder: () -> Unit, onOpenGame: () -> Unit,
                     onSettings: () -> Unit) {
    Surface(modifier = Modifier.fillMaxSize()) { /* the theme's background and text colours */
        Box(Modifier.fillMaxSize().systemBarsPadding()) {
            Column(
                modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
                verticalArrangement = Arrangement.Center,
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text("DreamBoy", style = MaterialTheme.typography.headlineMedium)
                Spacer(Modifier.height(20.dp))
                SlotBay(Modifier.widthIn(max = 420.dp)) {
                    EmptyCartOutline(if (folderLost) "Folder not found" else "No cartridge",
                        Modifier.fillMaxWidth().padding(start = 28.dp, end = 28.dp, top = 15.dp, bottom = 22.dp).height(150.dp))
                }
                Spacer(Modifier.height(24.dp))
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
                Button(onClick = onChooseFolder, modifier = Modifier.widthIn(min = 260.dp).heightIn(min = 56.dp)) {
                    Icon(Icons.Filled.FolderOpen, contentDescription = null)
                    Spacer(Modifier.width(10.dp))
                    Text("Choose games folder")
                }
                Spacer(Modifier.height(12.dp))
                OutlinedButton(onClick = onOpenGame, modifier = Modifier.widthIn(min = 260.dp).heightIn(min = 56.dp)) {
                    Text("Open one game")
                }
                if (message != null) {
                    Spacer(Modifier.height(16.dp))
                    Text(message, color = MaterialTheme.colorScheme.error, textAlign = TextAlign.Center)
                }
            }
            // After the full-screen column, so it is drawn on top and gets the tap.
            IconButton(onClick = onSettings, modifier = Modifier.align(Alignment.TopEnd).padding(4.dp)) {
                Icon(Icons.Filled.Settings, contentDescription = "Settings")
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
 * A cartridge's grip-strip line: the console's printed mark (once the console is known), then when it was played,
 * then "outside the folder": "GBC · Today". The mark comes first so a column of carts reads as a column of marks.
 */
fun gripLine(console: Console?, played: String, outside: Boolean = false): String =
    listOfNotNull(console?.mark, played, "outside the folder".takeIf { outside }).joinToString(" · ")

/** The grid's one-line form (12): "Today", "Yesterday", a date, or "Never played" — no time, so it fits with large text. */
fun playedDay(lastPlayed: Long, now: Long = System.currentTimeMillis()): String {
    if (lastPlayed <= 0) return "Never played"
    val day = { t: Long -> Calendar.getInstance().apply { timeInMillis = t }.let { it.get(Calendar.YEAR) * 1000 + it.get(Calendar.DAY_OF_YEAR) } }
    val yesterday = Calendar.getInstance().apply { timeInMillis = now; add(Calendar.DAY_OF_YEAR, -1) }.timeInMillis
    return when (day(lastPlayed)) {
        day(now) -> "Today"
        day(yesterday) -> "Yesterday"
        else -> DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(lastPlayed))
    }
}

/** Times and counts: even-width figures, so they line up. */
internal val Tabular = TextStyle(fontFeatureSettings = "tnum")

/**
 * The game list as a cartridge collection: the last-played game seated in the slot with Continue, then every other
 * game as a cartridge (its box art or the picture of where it was left as the label). A tap pushes the cartridge into
 * the slot, then plays; holding or the cartridge's ⋮ opens [onHold]'s sheet. [refresh] changes whenever the pictures
 * may have changed.
 */
@Composable
fun LibraryScreen(rows: List<GameEntry>, folderName: String?, scanning: Boolean, message: String?, refresh: Int,
                  picture: (GameEntry) -> Bitmap?, box: (GameEntry) -> Bitmap? = { null }, onPlay: (GameEntry) -> Unit,
                  onHold: (GameEntry) -> Unit, onOpenGame: () -> Unit, onChangeFolder: () -> Unit, onSettings: () -> Unit) {
    var menuOpen by remember { mutableStateOf(false) }
    var inserting by remember { mutableStateOf<String?>(null) } // the game being pushed into the slot
    val insert = { g: GameEntry -> if (inserting == null) inserting = g.key }
    val done = { g: GameEntry -> inserting = null; onPlay(g) }
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
                Text(message, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(horizontal = 20.dp, vertical = 4.dp))
            }
            if (rows.isEmpty()) {
                EmptyLibrary(folderName, scanning, onChangeFolder)
            } else {
                val (last, rest) = remember(rows) { Library.continueAndRest(rows) }
                LazyVerticalGrid(
                    columns = GridCells.Adaptive(152.dp), modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 24.dp),
                    horizontalArrangement = Arrangement.spacedBy(14.dp), verticalArrangement = Arrangement.spacedBy(16.dp),
                ) {
                    item(key = "slot:" + (last?.key ?: ""), span = { GridItemSpan(maxLineSpan) }) {
                        ContinueSlot(last, refresh, picture, inserting == last?.key, insert, done, onHold)
                    }
                    items(rest, key = { it.key }) { game ->
                        GameCartridge(game, refresh, picture, box, inserting == game.key, insert, done, onHold)
                    }
                }
            }
        }
    }
}

/** No games: still looking, or the folder is empty. */
@Composable
private fun EmptyLibrary(folderName: String?, scanning: Boolean, onChangeFolder: () -> Unit) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally) {
        SlotBay {
            Box(Modifier.fillMaxWidth().padding(start = 28.dp, end = 28.dp, top = 15.dp, bottom = 22.dp).height(170.dp),
                contentAlignment = Alignment.Center) {
                if (scanning) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        CircularProgressIndicator(color = Color.White)
                        Spacer(Modifier.height(14.dp))
                        Text("Looking for games", color = Color.White.copy(alpha = 0.78f))
                    }
                } else {
                    EmptyCartOutline("No games in ${folderName ?: "the folder"} yet", Modifier.fillMaxSize())
                }
            }
        }
        if (!scanning) {
            Spacer(Modifier.height(20.dp))
            Text("Put .gba, .gb, .gbc or .zip files there, or pick another folder.", textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(20.dp))
            Button(onClick = onChangeFolder, modifier = Modifier.heightIn(min = 52.dp)) {
                Icon(Icons.Filled.FolderOpen, contentDescription = null)
                Spacer(Modifier.width(10.dp))
                Text("Change folder")
            }
        }
    }
}

/**
 * The slot at the top of the list: [game] (the last played) seated in it — its label the picture of where it was
 * left, Continue under it. Inserting: the cartridge slides up into the slot's mouth, then [onInserted]. No game played
 * yet: an empty slot.
 */
@Composable
private fun ContinueSlot(game: GameEntry?, refresh: Int, picture: (GameEntry) -> Bitmap?, inserting: Boolean,
                         onInsert: (GameEntry) -> Unit, onInserted: (GameEntry) -> Unit, onHold: (GameEntry) -> Unit) {
    SlotBay {
        // The clip's top edge is the middle of the slot's mouth: what slides above it has gone into the slot.
        BoxWithConstraints(Modifier.fillMaxWidth().padding(top = 15.dp).clipToBounds().padding(bottom = 20.dp),
            contentAlignment = Alignment.TopCenter) {
            if (game == null) {
                EmptyCartOutline("Pick a game below to start",
                    Modifier.padding(horizontal = 28.dp).fillMaxWidth().height(150.dp))
                return@BoxWithConstraints
            }
            val bitmap = remember(game.id, refresh) { picture(game) }
            val travel = with(LocalDensity.current) { 64.dp.toPx() }
            val push = insertMotion(inserting) { onInserted(game) }
            val wide = maxWidth > 560.dp // sideways or a tablet: the label on the left, Continue beside it
            val width = if (wide) minOf(maxWidth - 40.dp, 680.dp) else maxWidth - 40.dp
            Cartridge(Modifier.width(width).graphicsLayer { translationY = -push.value * travel }.testTag("continue"),
                onClick = { onInsert(game) }, onLongClick = { onHold(game) }, lip = 9.dp, inset = 12.dp) {
                if (wide) {
                    // The label sized so the whole slot fits under the header on a short, wide screen.
                    val screen = LocalConfiguration.current.screenHeightDp.dp
                    val label = ((screen - 270.dp) * 1.5f).coerceIn(180.dp, 300.dp)
                    Row(Modifier.height(IntrinsicSize.Min)) {
                        SeatedLabel(game, bitmap, push.value, onHold, Modifier.width(label), lines = 2)
                        Column(Modifier.weight(1f).fillMaxHeight().padding(start = 20.dp)) {
                            Spacer(Modifier.weight(1f))
                            PlayedText(game, Modifier.padding(bottom = 12.dp))
                            ContinueButton { onInsert(game) }
                        }
                    }
                    Spacer(Modifier.fillMaxWidth().height(24.dp).gripRidges(LocalPak.current.shade))
                } else {
                    SeatedLabel(game, bitmap, push.value, onHold, Modifier)
                    Row(Modifier.fillMaxWidth().gripRidges(LocalPak.current.shade).padding(top = 12.dp, bottom = 22.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        ContinueButton { onInsert(game) }
                        Spacer(Modifier.weight(1f))
                        PlayedText(game, Modifier.padding(start = 12.dp))
                    }
                }
            }
        }
    }
}

/**
 * The seated cartridge's label: the band with the name and ⋮, the picture of where the game was left. [lines]: 2 on the
 * narrower sideways label, so the name is never cut short.
 */
@Composable
private fun SeatedLabel(game: GameEntry, bitmap: Bitmap?, push: Float, onHold: (GameEntry) -> Unit, modifier: Modifier,
                        lines: Int = 1) {
    Column(modifier.clip(RoundedCornerShape(4.dp))) {
        LabelBand(game.name, big = true, lines = lines) {
            IconButton(onClick = { onHold(game) }) { Icon(Icons.Filled.MoreVert, contentDescription = "${game.name}: more") }
        }
        Box {
            LeftPicture(bitmap, big = true, Modifier.fillMaxWidth(), corners = 0.dp)
            Box(Modifier.matchParentSize().background(Color.Black.copy(alpha = push * 0.6f)))
        }
    }
}

@Composable
private fun ContinueButton(onClick: () -> Unit) {
    Button(onClick = onClick, modifier = Modifier.heightIn(min = 48.dp), contentPadding = PaddingValues(start = 18.dp, end = 24.dp)) {
        Icon(Icons.Filled.PlayArrow, contentDescription = null)
        Spacer(Modifier.width(6.dp))
        Text("Continue", style = MaterialTheme.typography.titleSmall.copy(fontSize = 17.sp))
    }
}

@Composable
private fun PlayedText(game: GameEntry, modifier: Modifier) {
    Text(gripLine(game.console, playedText(game.lastPlayed)), style = MaterialTheme.typography.bodyMedium.merge(Tabular),
        color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 2, modifier = modifier)
}

/**
 * The insert (12): 0 → 1 over 260 ms once [inserting] turns true, then [onDone]; back to 0 a moment later (in case the
 * game didn't open, e.g. it asks about a save first). With Android's animations off it is instant.
 */
@Composable
private fun insertMotion(inserting: Boolean, onDone: () -> Unit): Animatable<Float, AnimationVector1D> {
    val push = remember { Animatable(0f) }
    LaunchedEffect(inserting) {
        if (!inserting) return@LaunchedEffect
        push.animateTo(1f, tween(260, easing = CubicBezierEasing(0.3f, 0f, 0.8f, 0.15f)))
        onDone()
        delay(400)
        push.snapTo(0f)
    }
    return push
}

/** One game of the collection: a cartridge with its box (or the picture of where it was left) as the label. */
@Composable
private fun GameCartridge(game: GameEntry, refresh: Int, picture: (GameEntry) -> Bitmap?, box: (GameEntry) -> Bitmap?,
                          inserting: Boolean, onInsert: (GameEntry) -> Unit, onInserted: (GameEntry) -> Unit,
                          onHold: (GameEntry) -> Unit) {
    val bitmap = remember(game.id, refresh) { picture(game) }
    val boxBitmap = remember(game.id, refresh) { box(game) }
    val push = insertMotion(inserting) { onInserted(game) }
    val travel = with(LocalDensity.current) { 14.dp.toPx() }
    Cartridge(Modifier.fillMaxWidth().graphicsLayer {
        translationY = -push.value * travel
        val s = 1f - push.value * 0.04f
        scaleX = s
        scaleY = s
    }.testTag("game"), onClick = { onInsert(game) }, onLongClick = { onHold(game) }, lip = 6.dp, inset = 8.dp) {
        Column(Modifier.clip(RoundedCornerShape(4.dp))) {
            LabelBand(game.name)
            // The label: the box on printed label stock, else the picture of where it was left filling it, else blank stock.
            Box(Modifier.fillMaxWidth().aspectRatio(1f).background(Color(Palette.LABEL_STOCK)), contentAlignment = Alignment.Center) {
                when {
                    boxBitmap != null -> Image(boxBitmap.asImageBitmap(), contentDescription = "Box art", modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.Fit)
                    bitmap != null -> Image(bitmap.asImageBitmap(), contentDescription = null, modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.Crop, filterQuality = FilterQuality.None)
                    else -> BlankLabel(big = false)
                }
                Box(Modifier.matchParentSize().background(Color.Black.copy(alpha = push.value * 0.6f)))
            }
        }
        Row(Modifier.fillMaxWidth().gripRidges(LocalPak.current.shade).padding(start = 2.dp, bottom = 12.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text(gripLine(game.console, playedDay(game.lastPlayed), game.outside),
                style = MaterialTheme.typography.bodySmall.merge(Tabular), color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
            IconButton(onClick = { onHold(game) }) {
                Icon(Icons.Filled.MoreVert, contentDescription = "${game.name}: more", tint = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

/** The picture of where the game was left (sharp pixels, in its own shape inside the 3:2 frame — a Game Boy's
 *  picture is narrower: the label's paper shows beside it), or a placeholder. */
@Composable
private fun LeftPicture(bitmap: Bitmap?, big: Boolean, modifier: Modifier, corners: Dp = 8.dp) {
    val frame = modifier.aspectRatio(1.5f).clip(RoundedCornerShape(corners))
    if (bitmap != null) {
        Image(bitmap.asImageBitmap(), contentDescription = null, modifier = frame.background(Color(Palette.LABEL_STOCK)),
            contentScale = ContentScale.Fit, filterQuality = FilterQuality.None)
    } else {
        Box(frame.background(Color(Palette.LABEL_STOCK)), contentAlignment = Alignment.Center) { BlankLabel(big) }
    }
}

/** A label with no picture yet: the printed stock with a faint controller mark. */
@Composable
private fun BlankLabel(big: Boolean) {
    Icon(Icons.Filled.SportsEsports, contentDescription = null, tint = Color(Palette.LIGHT.inkSoft).copy(alpha = 0.5f),
        modifier = Modifier.size(if (big) 48.dp else 36.dp))
}

/** A game's sheet (holding it, or its ⋮). [onRemove] only for games from outside the folder. */
@Composable
fun GameHoldMenu(game: GameEntry, onStartFresh: () -> Unit, onImport: () -> Unit, onExport: () -> Unit,
                 onGameSettings: () -> Unit, onRemove: (() -> Unit)?, onClose: () -> Unit, hasBox: Boolean = false,
                 onChooseBox: () -> Unit = {}, onRemoveBox: () -> Unit = {}) {
    ActionSheet(game.name, listOfNotNull(
        SheetItem(Icons.Filled.Replay, "Start fresh (from the title screen)", onClick = onStartFresh),
        SheetItem(Icons.Filled.FileDownload, "Import save", onClick = onImport),
        SheetItem(Icons.Filled.FileUpload, "Export save", onClick = onExport),
        // (a Game Boy cartridge says itself what it saves with, and its clock is always on: nothing to set)
        if (game.console == null || game.console == Console.GBA) SheetItem(Icons.Filled.Tune, "Game settings", onClick = onGameSettings) else null,
        SheetItem(Icons.Filled.Image, "Choose box art", onClick = onChooseBox),
        if (hasBox) SheetItem(Icons.Filled.HideImage, "Remove box art", onClick = onRemoveBox) else null,
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
enum class MenuPage { MAIN, SAVE, LOAD, CHEATS, ACHIEVEMENTS, LEADERBOARDS }

/**
 * A dimmed layer over the paused game (it takes every touch) with a cartridge-plastic panel in the middle; it
 * appears with a quick fade and grow.
 */
@Composable
internal fun MenuLayer(maxWidth: Dp = 440.dp, content: @Composable ColumnScope.() -> Unit) {
    val shown = remember { Animatable(0f) }
    LaunchedEffect(Unit) { shown.animateTo(1f, tween(150)) }
    Box(
        Modifier.fillMaxSize().graphicsLayer { alpha = shown.value }.background(Scrim)
            .pointerInput(Unit) { detectTapGestures { } }.systemBarsPadding(),
        contentAlignment = Alignment.Center,
    ) {
        Cartridge(Modifier.padding(16.dp).widthIn(max = maxWidth).graphicsLayer {
            val s = 0.96f + 0.04f * shown.value
            scaleX = s
            scaleY = s
        }, lip = 10.dp, inset = 14.dp, content = content)
    }
}

/**
 * Back while playing: the game is paused behind this menu. [fastSpeed]: 2, 4, 8 or NativeBridge.FF_MAX. Sideways (a
 * short, wide screen) it has two columns, so nothing is below the fold.
 */
@Composable
fun GameMenu(title: String, fastSpeed: Int, onFastSpeed: (Int) -> Unit, onResume: () -> Unit, onSave: () -> Unit,
             onLoad: () -> Unit, onCheats: () -> Unit, onSettings: () -> Unit, onRestart: () -> Unit, onQuit: () -> Unit,
             locked: Boolean = false, onAchievements: (() -> Unit)? = null, onLeaderboards: (() -> Unit)? = null,
             cheatsOff: String? = null) {
    BoxWithConstraints {
        val wide = maxWidth > maxHeight && maxHeight < 520.dp
        MenuLayer(maxWidth = if (wide) 720.dp else 420.dp) {
            if (wide) {
                LabelBand(title, Modifier.clip(RoundedCornerShape(4.dp)), big = true)
                Row(Modifier.padding(top = 14.dp, bottom = 16.dp), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    Column(Modifier.weight(1f)) {
                        ResumeButton(onResume)
                        SpeedChoices(fastSpeed, onFastSpeed)
                        RestartQuit(onRestart, onQuit)
                    }
                    Column(Modifier.weight(1f).align(Alignment.CenterVertically)) {
                        MenuTilesCut(onSave, onLoad, onCheats, onSettings, locked, onAchievements, onLeaderboards, cheatsOff)
                    }
                }
            } else {
                Column(Modifier.widthIn(min = 280.dp, max = 392.dp).verticalScroll(rememberScrollState()).padding(bottom = 16.dp)) {
                    LabelBand(title, Modifier.clip(RoundedCornerShape(4.dp)), big = true, lines = 2)
                    Spacer(Modifier.height(16.dp))
                    ResumeButton(onResume)
                    Spacer(Modifier.height(14.dp))
                    MenuTiles(onSave, onLoad, onCheats, onSettings, locked, onAchievements, onLeaderboards, cheatsOff)
                    SpeedChoices(fastSpeed, onFastSpeed)
                    RestartQuit(onRestart, onQuit)
                }
            }
        }
    }
}

@Composable
private fun ResumeButton(onResume: () -> Unit) {
    Button(onClick = onResume, modifier = Modifier.fillMaxWidth().heightIn(min = 56.dp)) {
        Icon(Icons.Filled.PlayArrow, contentDescription = null)
        Spacer(Modifier.width(8.dp))
        Text("Resume", style = MaterialTheme.typography.titleMedium)
    }
}

@Composable
private fun MenuTiles(onSave: () -> Unit, onLoad: () -> Unit, onCheats: () -> Unit, onSettings: () -> Unit,
                      locked: Boolean = false, onAchievements: (() -> Unit)? = null, onLeaderboards: (() -> Unit)? = null,
                      cheatsOff: String? = null) {
    val rows = menuRows(onSave, onLoad, onCheats, onSettings, locked, onAchievements, onLeaderboards, cheatsOff)
    Column {
        rows.forEachIndexed { i, row ->
            if (i > 0) HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
            row()
        }
    }
}

/** Sideways the rows sit in their own column; on a short screen it scrolls, cut partway through a row so it plainly goes on. */
@Composable
private fun ColumnScope.MenuTilesCut(onSave: () -> Unit, onLoad: () -> Unit, onCheats: () -> Unit, onSettings: () -> Unit,
                                     locked: Boolean, onAchievements: (() -> Unit)?, onLeaderboards: (() -> Unit)?,
                                     cheatsOff: String?) {
    val rows = menuRows(onSave, onLoad, onCheats, onSettings, locked, onAchievements, onLeaderboards, cheatsOff)
    CutList(rows.size, rows.size) { i -> rows[i]() }
}

/** The pause menu's rows, in order: Save state, Load state, Achievements and Leaderboards (games that have them), Cheats, Settings.
 *  [cheatsOff]: why this game has no cheats (a Game Boy game, for now) — the row stays in place, off, with that under it. */
private fun menuRows(onSave: () -> Unit, onLoad: () -> Unit, onCheats: () -> Unit, onSettings: () -> Unit, locked: Boolean,
                     onAchievements: (() -> Unit)?, onLeaderboards: (() -> Unit)?, cheatsOff: String?): List<@Composable () -> Unit> {
    val off = "Off in hardcore mode"
    return buildList {
        add { MenuRow(Icons.Filled.Save, "Save state", onSave) }
        add { MenuRow(Icons.Filled.FolderOpen, "Load state", onLoad, enabled = !locked, note = if (locked) off else null) }
        if (onAchievements != null) add { MenuRow(Icons.Filled.EmojiEvents, "Achievements", onAchievements) }
        if (onLeaderboards != null) add { MenuRow(Icons.Filled.Leaderboard, "Leaderboards", onLeaderboards) }
        add { MenuRow(Icons.Filled.AutoFixHigh, "Cheats", onCheats, enabled = !locked && cheatsOff == null,
            note = cheatsOff ?: if (locked) off else null) }
        add { MenuRow(Icons.Filled.Settings, "Settings", onSettings) }
    }
}

@Composable
private fun SpeedChoices(fastSpeed: Int, onFastSpeed: (Int) -> Unit) {
    Spacer(Modifier.height(18.dp))
    Text("Fast-forward speed", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
    Spacer(Modifier.height(8.dp))
    Choices(Settings.SPEEDS.map(Settings::speedName), Settings.SPEEDS.indexOf(fastSpeed).coerceAtLeast(0),
        onPick = { onFastSpeed(Settings.SPEEDS[it]) })
}

@Composable
private fun RestartQuit(onRestart: () -> Unit, onQuit: () -> Unit) {
    Spacer(Modifier.height(18.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        OutlinedButton(onClick = onRestart, modifier = Modifier.weight(1f).heightIn(min = 48.dp),
            colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.onSurface)) {
            Icon(Icons.Filled.RestartAlt, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(6.dp))
            Text("Restart")
        }
        FilledTonalButton(onClick = onQuit, modifier = Modifier.weight(1f).heightIn(min = 48.dp),
            colors = ButtonDefaults.filledTonalButtonColors(containerColor = MaterialTheme.colorScheme.error,
                contentColor = MaterialTheme.colorScheme.onError)) {
            Icon(Icons.AutoMirrored.Filled.ExitToApp, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(6.dp))
            Text("Quit")
        }
    }
}

/**
 * The 10 slots as save stickers (two columns): the slot's number big, the picture, when it was saved. Saving: every
 * slot can be picked; loading: only used ones, the Quick one (a Delta skin's quick save, 9) first. [refresh] changes
 * whenever the slots may have changed, so the pictures are read again.
 */
@Composable
fun SlotGrid(saving: Boolean, states: States, refresh: Int, onPick: (Int) -> Unit, onBack: () -> Unit) {
    val all = remember(saving) { (if (saving) emptyList() else listOf(States.QUICK)) + (1..States.SLOTS) }
    val used = remember(refresh, all) { all.associateWith { states.used(it) } }
    val pictures = remember(refresh, all) { all.associateWith { if (used.getValue(it)) states.loadPicture(it) else null } }
    val dates = remember(refresh, all) {
        val day = DateFormat.getDateInstance(DateFormat.SHORT)
        val time = DateFormat.getTimeInstance(DateFormat.SHORT)
        all.associateWith { if (used.getValue(it)) Date(states.savedAt(it)).let { d -> day.format(d) to time.format(d) } else null }
    }
    val slots = all.filter { saving || used.getValue(it) }
    MenuLayer {
        // At least two stickers wide, so a page with one saved state doesn't shrink to a narrow strip.
        Column(Modifier.widthIn(min = 300.dp).padding(bottom = 12.dp)) {
            ScreenHeader(if (saving) "Save state" else "Load state", onBack = onBack,
                subtitle = if (saving) "Pick a slot" else null)
            Column(Modifier.verticalScroll(rememberScrollState())) {
                if (slots.isEmpty()) {
                    Text("No saved states yet. Use Save state first.", color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(12.dp))
                }
                for (pair in slots.chunked(2)) {
                    Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.height(IntrinsicSize.Min).padding(vertical = 6.dp)) {
                        for (slot in pair) SaveSticker(slot, pictures[slot], dates[slot], onPick, Modifier.weight(1f).fillMaxHeight())
                        if (pair.size == 1) Spacer(Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

/**
 * One slot as a save sticker: printed label stock (the same in both themes) with the picture and when it was saved, and
 * the slot's number big in label lettering. An empty slot has no sticker printed yet: its number and "Empty" on the
 * plastic (so the save page stays dark at night), stretched to its neighbour's height.
 */
@Composable
private fun SaveSticker(slot: Int, picture: Bitmap?, date: Pair<String, String>?, onPick: (Int) -> Unit, modifier: Modifier) {
    val name = if (slot == States.QUICK) "Quick" else "Slot $slot"
    val printed = picture != null
    val ink = if (printed) Color(Palette.LIGHT.ink) else MaterialTheme.colorScheme.onSurface
    val soft = if (printed) Color(Palette.LIGHT.inkSoft) else MaterialTheme.colorScheme.onSurfaceVariant
    Column(modifier.clip(RoundedCornerShape(6.dp)).background(if (printed) Color(Palette.LABEL_STOCK) else LocalPak.current.cartHigh)
        .clickable(onClickLabel = name) { onPick(slot) }.padding(start = 7.dp, end = 7.dp, top = 7.dp, bottom = 2.dp)) {
        if (picture != null) {
            Image(picture.asImageBitmap(), contentDescription = name.lowercase(),
                modifier = Modifier.fillMaxWidth().aspectRatio(1.5f).clip(RoundedCornerShape(3.dp)),
                contentScale = ContentScale.Fit, filterQuality = FilterQuality.None)
            Text(date?.let { "${it.first} · ${it.second}" } ?: "", style = MaterialTheme.typography.bodySmall.merge(Tabular),
                color = soft, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.padding(start = 1.dp, top = 6.dp))
        } else {
            Text("Empty", style = MaterialTheme.typography.labelMedium, color = soft, modifier = Modifier.padding(start = 1.dp))
            Spacer(Modifier.weight(1f))
        }
        Text(if (slot == States.QUICK) "Q" else "$slot", style = MaterialTheme.typography.displayLarge.merge(Tabular), color = ink,
            maxLines = 1, modifier = Modifier.semantics { contentDescription = name })
    }
}

/** The type picker's names, by NativeBridge.CHEAT_* value. */
val CHEAT_TYPES = listOf("Auto", "Action Replay v3", "GameShark v1/v2", "CodeBreaker", "address:value")

/**
 * The game's cheats: one row each (name, on/off switch); a tap on the name edits it. Below: Add cheat, Import cheat
 * file and (with cheats) Remove all, which asks first. The list shrinks to leave the buttons room on a short screen.
 */
@Composable
fun CheatPanel(title: String, cheats: List<Cheat>, onToggle: (Int, Boolean) -> Unit, onEdit: (Int) -> Unit,
               onAdd: () -> Unit, onBack: () -> Unit, onImport: () -> Unit = {}, onRemoveAll: () -> Unit = {},
               onFindOnline: () -> Unit = {}) {
    var askRemoveAll by remember { mutableStateOf(false) }
    MenuLayer {
        Column(Modifier.padding(bottom = 16.dp).widthIn(min = 300.dp)) {
            ScreenHeader("Cheats", onBack = onBack, subtitle = title)
            Column(Modifier.weight(1f, fill = false)) {
                Column(Modifier.weight(1f, fill = false).heightIn(max = 360.dp).verticalScroll(rememberScrollState())) {
                    if (cheats.isEmpty()) {
                        Text("No cheats yet. Add Action Replay, GameShark or CodeBreaker codes.",
                            color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(vertical = 8.dp))
                    }
                    if (cheats.isNotEmpty()) Column {
                        cheats.forEachIndexed { i, c ->
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(c.name, Modifier.weight(1f).clickable { onEdit(i) }.padding(vertical = 16.dp, horizontal = 4.dp),
                                    maxLines = 2, overflow = TextOverflow.Ellipsis)
                                Switch(checked = c.on, onCheckedChange = { onToggle(i, it) }, modifier = Modifier.padding(end = 4.dp))
                            }
                            if (i < cheats.lastIndex) HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                        }
                    }
                }
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
                Spacer(Modifier.height(8.dp))
                OutlinedButton(onClick = onFindOnline, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Icon(Icons.Filled.CloudDownload, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Find cheats online")
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

/**
 * The game (a SurfaceView handed to the native render thread) and the touch buttons, in layout A; for a solar
 * cartridge also the sun button with the light slider ([manualLight] null = Auto, the phone's light sensor).
 */
@Composable
fun GameScreen(controlsHidden: Boolean, fastOn: Boolean, onTouchMask: (Int) -> Unit, onShowControls: () -> Unit,
               onDrawFailed: () -> Unit, solar: Boolean = false, manualLight: Int? = null,
               onManualLight: (Int?) -> Unit = {}, touchLook: Int = Settings.LOOK_NORMAL, buzz: Boolean = true,
               layouts: LayoutStore.Layouts = LayoutStore.Layouts(), showSpeed: Boolean = false, skin: Skin = Skins.PLAIN,
               fitMode: Int = Settings.SIZE_FIT, delta: SkinFiles.Imported? = null, onAction: (DeltaSkin.Action) -> Unit = {},
               onSkinFailed: () -> Unit = {}, console: Console = Console.GBA,
               overlay: @Composable BoxScope.() -> Unit = {}) {
    // The skin's body round the picture: here, and inside the game's surface by OpenGL (NativeBridge.setBackground);
    // black with a Delta skin (9).
    BoxWithConstraints(Modifier.fillMaxSize().background(if (delta != null) Color.Black else Color(skin.body))) {
        val sideways = maxWidth > maxHeight
        val edits = layouts.of(sideways, console.family)
        val density = LocalDensity.current
        val w = constraints.maxWidth.toFloat()
        val h = constraints.maxHeight.toFloat()
        // A Delta skin's layout for this direction (none, or its picture failed: the Plain buttons with the user's layout).
        var failed by remember(delta) { mutableStateOf(setOf<Boolean>()) }
        val face = if (sideways in failed) null else delta?.skin?.face(sideways)
        // The game's surface in the Delta skin's game box, or the layout's picture box (upright by default: full width
        // at the top; sideways: the whole screen, the game fitted inside); the buttons over the whole screen.
        val pic = if (face != null) DeltaLayout(face, w, h, sideways).game
            else TouchLayout(w, h, density.density, sideways, edits, console = console).picture
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
                v.console = console
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
        if (solar) SunControl(manualLight, onManualLight, Modifier.align(Alignment.BottomStart))
        if (showSpeed) SpeedLabel(Modifier.align(Alignment.TopCenter))
        overlay()
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
        Text(it, color = Color.White, style = MaterialTheme.typography.labelMedium.merge(Tabular),
            modifier = modifier.padding(top = 6.dp).background(Scrim, RoundedCornerShape(50))
                .padding(horizontal = 12.dp, vertical = 4.dp))
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
            colors = IconButtonDefaults.filledTonalIconButtonColors(containerColor = Scrim, contentColor = Color.White)) {
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

/** Hardcore switched on in a game with achievements (14): it only counts from a fresh start. */
@Composable
fun HardcoreRestartDialog(onRestart: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Restart the game?") },
        text = { Text("Hardcore mode starts from the title screen. Anything not saved in the game is lost.") },
        confirmButton = { DangerButton("Restart", onRestart) },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}
