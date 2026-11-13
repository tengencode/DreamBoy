package com.tengen.gbaemu

import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.DocumentsContract
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
import java.io.InputStream
import java.nio.ByteBuffer
import java.text.DateFormat
import java.util.Date
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
    private lateinit var skinFiles: SkinFiles // the imported Delta skins (9)
    private var importedSkins by mutableStateOf(listOf<SkinFiles.Imported>())
    private var skinTiles by mutableStateOf(listOf<ImportedSkin>()) // as the Skin page shows them
    private var skinError by mutableStateOf<String?>(null) // why an import failed
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
    private lateinit var boxArt: BoxArt // the game list's box art (10b)
    private var boxHelper: BoxArtHelper? = null // looking up box art while the game list shows
    private var boxFor: GameEntry? = null // the game whose box is being chosen
    private var currentCode = "" // the running game's header code (for Find cheats online)
    private var onlineCheats by mutableStateOf<OnlineCheats?>(null)
    private lateinit var ra: Achievements // RetroAchievements (14)
    private lateinit var badges: BadgeCache
    private var raUi by mutableStateOf(RaUi())
    private val raPopups = RaPopups()
    private var raView by mutableStateOf(RaPopups.View())
    private var raSet by mutableStateOf(false) // the game being played has achievements
    private var hardcoreOn by mutableStateOf(false) // and the hardcore limits apply to it
    private var askHardcoreRestart by mutableStateOf(false)
    private var raList by mutableStateOf<Pair<IntArray?, List<RaAchievement>>>(null to emptyList()) // the Achievements panel's
    private var raBoards by mutableStateOf(emptyList<RaBoard>()) // the game's leaderboards (15); empty: no Leaderboards row
    private var openBoard by mutableStateOf<RaBoard?>(null)
    private var boardScores by mutableStateOf<BoardScores?>(null)
    private var boardAsk by mutableStateOf(0) // a new number asks the server again (Try again)
    private lateinit var backups: Restore // backing up and restoring (11)
    private lateinit var backupSchedule: BackupSchedule
    private var backupStatus by mutableStateOf(BackupStatus())
    private var opening = false // a game is being read (open)
    private var busy by mutableStateOf<String?>(null) // the spinner's text while a backup is made, checked or restored
    private var restoreInfo by mutableStateOf<Backup.Info?>(null) // a checked backup waiting for the user's yes
    private var askUndo by mutableStateOf(false)
    private var autoRunning = false
    private var autoFailSaid = false // "Automatic backup failed" is said once per run
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
    private val pickSkin = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importSkin(uri)
    }
    private val pickCht = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importCheats(uri)
    }
    private val pickBox = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) boxChosen(uri)
    }
    private val backupTo = registerForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri ->
        if (uri != null) backUpTo(uri)
    }
    private val pickBackup = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) checkBackup(uri)
    }
    private val pickBackupFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) useBackupFolder(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        library = Library(filesDir)
        boxArt = BoxArt(File(filesDir, "boxart"))
        if (!library.load()) toast("The game list couldn't be read; it was kept aside and is built again from the folder.")
        carryOverLastGame()
        rows = library.rows()
        hasFolder = library.folder != null
        settingsStore = PrefsStore(getSharedPreferences("settings", MODE_PRIVATE))
        settings = Settings.load(settingsStore)
        fastSpeed = settings.fastSpeed
        skinFiles = SkinFiles(filesDir, SkinPicture::thumbnail)
        refreshSkins()
        if (settings.skinImport != 0 && activeSkin() == null) { // its folder is gone or unreadable: the built-in skin
            settings = settings.copy(skinImport = 0)
            settings.save(settingsStore)
        }
        applySettings(settings)
        // RetroAchievements (14): the internet (or, in a debug build with files/ra-fake, the pretend server) and the saved sign-in.
        val debuggable = applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE != 0
        val fake = debuggable && File(filesDir, "ra-fake").exists()
        Net.userAgent = "DreamBoy/${appVersion()} (Android)"
        val userAgent = RaNet.userAgent(appVersion(), android.os.Build.VERSION.RELEASE, NativeBridge.raUserAgentClause())
        if (fake) FakeRaServer.offlineFlag = File(filesDir, "ra-fake-offline")
        RaService.worker.send = if (fake) FakeRaServer::reply else { r -> RaNet.send(r, userAgent) }
        val outbox = RaService.worker.outbox ?: RaOutbox(File(filesDir, "ra-outbox.json")).also { RaService.worker.outbox = it }
        RaService.worker.start()
        if (outbox.size > 0) thread(isDaemon = true, name = "ra-outbox") { outbox.resend(RaService.worker.send) } // from an earlier run
        badges = BadgeCache(File(filesDir, "ra/badges"),
            if (fake) FakeBadges::png else { url -> (Net.get(url, 512 * 1024) as? Net.Got)?.bytes })
        ra = Achievements(NativeRa, RaAccount(File(filesDir, "ra.json")))
        ra.start()
        watchSignIn()
        val keysStore = PrefsStore(getSharedPreferences("keys", MODE_PRIVATE))
        keys = KeyBindings(keysStore)
        keys.use()
        val gameStore = PrefsStore(getSharedPreferences("game_settings", MODE_PRIVATE))
        gameSettings = GameSettings(gameStore)
        backups = Restore(filesDir, mapOf("settings" to settingsStore, "keys" to keysStore, "game_settings" to gameStore), boxArt)
        backupSchedule = BackupSchedule(File(filesDir, "backup-state.json"))
        refreshBackupStatus()
        layoutStore = LayoutStore(filesDir)
        layouts = layoutStore.load()
        if (layoutStore.wasDamaged) toast("The touch layout couldn't be read; it was kept aside and the default layout is used.")
        setContent {
            // The accent follows the console skin (12); an imported Delta skin uses the default.
            AppTheme(accentSeed = AccentColors.seedFor(if (settings.skinImport != 0) null else Skins.at(settings.skin).id)) {
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
                        delta = activeSkin(),
                        console = gameConsole,
                        onAction = ::skinAction,
                        onSkinFailed = { toast("This skin's picture could not be drawn") },
                        overlay = { RaPopupLayer(raView, badges, belowSpeedLabel = settings.showSpeed) },
                    )
                    LaunchedEffect(Unit) { // RetroAchievements' events and the pop-ups' timing (14)
                        while (true) {
                            val now = SystemClock.uptimeMillis()
                            var changed = false
                            while (true) {
                                val e = RaEvent.of(NativeBridge.raTakeEvent()) ?: break
                                raPopups.add(e, now)
                                changed = true
                            }
                            if (raPopups.tick(now) || changed) raView = raPopups.view()
                            delay(100)
                        }
                    }
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
                        box = ::boxOf,
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
                        onClose = { holdGame = null },
                        hasBox = g.id.isNotEmpty() && boxArt.picture(g.id) != null,
                        onChooseBox = { holdGame = null; boxFor = g; pickBox.launch(arrayOf("image/*")) },
                        onRemoveBox = {
                            holdGame = null
                            idOf(g)?.let { boxArt.remove(it) }
                            libraryRefresh++
                        })
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
                        onRestart = { askRestart = true }, onQuit = ::quitGame,
                        locked = hardcoreOn, onAchievements = if (raSet) ({
                            raList = NativeBridge.raSummary() to RaAchievement.list(NativeBridge.raAchievements())
                            menu = MenuPage.ACHIEVEMENTS
                        }) else null,
                        onLeaderboards = if (raBoards.isNotEmpty()) ({ openBoard = null; menu = MenuPage.LEADERBOARDS }) else null)
                    MenuPage.SAVE -> if (st != null) SlotGrid(saving = true, st, slotsChanged,
                        onPick = { if (st.used(it)) replaceSlot = it else saveTo(it) }, onBack = { menu = MenuPage.MAIN })
                    MenuPage.LOAD -> if (st != null) SlotGrid(saving = false, st, slotsChanged,
                        onPick = ::loadFrom, onBack = { menu = MenuPage.MAIN })
                    MenuPage.CHEATS -> CheatPanel(currentTitle, cheats,
                        onToggle = { i, on -> changeCheats(cheats.toMutableList().also { it[i] = it[i].copy(on = on) }) },
                        onEdit = { editCheat = it }, onAdd = { editCheat = -1 }, onBack = { menu = MenuPage.MAIN },
                        onImport = { pickCht.launch(arrayOf("*/*")) }, onRemoveAll = { changeCheats(emptyList()) },
                        onFindOnline = ::findCheatsOnline)
                    MenuPage.ACHIEVEMENTS -> AchievementsPanel(String(NativeBridge.raGameTitle(), Charsets.UTF_8).ifEmpty { currentTitle },
                        raList.first, raList.second, hardcoreOn, badges, onBack = { menu = MenuPage.MAIN })
                    MenuPage.LEADERBOARDS -> {
                        LeaderboardsPanel(String(NativeBridge.raGameTitle(), Charsets.UTF_8).ifEmpty { currentTitle }, raBoards,
                            hardcore = hardcoreOn, open = openBoard, scores = boardScores,
                            onOpen = { openBoard = it; boardAsk++ }, onRetry = { boardAsk++ },
                            onBack = { if (openBoard != null) openBoard = null else menu = MenuPage.MAIN })
                        val b = openBoard
                        if (b != null) LaunchedEffect(b.id, boardAsk) { // the board's scores, asked for and waited on
                            boardScores = BoardScores.Loading
                            NativeBridge.raFetchEntries(b.id)
                            while (true) {
                                val s = BoardScores.of(NativeBridge.raEntriesStatus(),
                                    { String(NativeBridge.raEntriesMessage(), Charsets.UTF_8) },
                                    { RaEntry.list(NativeBridge.raEntries()) })
                                boardScores = s
                                if (s !is BoardScores.Loading) break
                                delay(100)
                            }
                        }
                    }
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
                onlineCheats?.let { OnlineCheatsDialog(it, onPick = ::pickOnlineCheats, onClose = { onlineCheats = null }) }
                restoreInfo?.let { info ->
                    RestoreDialog(restoreText(info, DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(info.made))),
                        onRestore = ::restoreNow, onCancel = { restoreInfo = null; backups.discard() })
                }
                if (askUndo) UndoRestoreDialog(onUndo = ::undoRestore, onCancel = { askUndo = false })
                busy?.let { BusyDialog(it) }
                replaceSlot?.let { slot -> ReplaceDialog(slot, onReplace = { saveTo(slot) }, onCancel = { replaceSlot = null }) }
                if (askRestart) RestartDialog(onRestart = ::restartGame, onCancel = { askRestart = false })
                if (askHardcoreRestart) HardcoreRestartDialog(
                    onRestart = {
                        askHardcoreRestart = false
                        settings = settings.copy(hardcore = true)
                        settings.save(settingsStore)
                        applySettings(settings)
                        settingsPage = null
                        restartGame()
                    },
                    onCancel = { askHardcoreRestart = false })
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
                        onEditLayout = ::openLayoutEditor, importedSkins = skinTiles,
                        onImportSkin = { pickSkin.launch(arrayOf("*/*")) }, onRemoveSkin = ::removeSkin,
                        backup = backupStatus.copy(inGame = playing),
                        onBackUp = { backupTo.launch(BackupSchedule.fileName(System.currentTimeMillis())) },
                        onRestore = { pickBackup.launch(arrayOf("*/*")) },
                        onUndoRestore = { askUndo = true },
                        onAutoBackup = { on -> if (on) pickBackupFolder.launch(null) else stopAutoBackup() },
                        onChangeFolder = { pickBackupFolder.launch(null) },
                        ra = raUi, onRaSignIn = { u, p -> ra.signIn(u, p); watchSignIn() },
                        onRaSignOut = { ra.signOut(); RaService.worker.outbox?.clear(); refreshRaUi(); hardcoreOn = false; raSet = false; raBoards = emptyList(); openBoard = null },
                        version = appVersion(), onOpenGithub = ::openGithub)
                }
                editLayout?.let { side ->
                    LayoutEditor(side, layouts.of(side, gameConsole.family), editPicture, settings.touchLook,
                        onSave = { edits ->
                            layouts = layouts.with(side, edits, gameConsole.family)
                            if (!layoutStore.save(layouts)) toast("Couldn't save the layout.")
                            closeLayoutEditor()
                        },
                        onCancel = ::closeLayoutEditor, skin = if (activeSkin() != null) Skins.PLAIN else Skins.at(settings.skin),
                        fitMode = settings.size, console = gameConsole)
                }
                waitingKey?.let { (bit, device) ->
                    KeyWaitLayer(KeyMap.bindings.first { it.bit == bit }.button, device, onCancel = { waitingKey = null })
                }
                unusualBios?.let { bytes ->
                    UnusualBiosDialog(onUse = { unusualBios = null; installBios(bytes) }, onCancel = { unusualBios = null })
                }
                skinError?.let { SkinErrorDialog(it, onOk = { skinError = null }) }
                editGame?.let { (name, id, game) ->
                    GameSettingsDialog(name, game, gameSettings.detected(id),
                        onChange = { gameSettings.set(id, it); editGame = Triple(name, id, it) },
                        onDone = { editGame = null })
                }
                BackHandler(enabled = playing && menu == null && settingsPage == null) { openMenu() }
                BackHandler(enabled = playing && menu != null && settingsPage == null) {
                    if (menu == MenuPage.MAIN) resumeGame()
                    else if (menu == MenuPage.LEADERBOARDS && openBoard != null) openBoard = null
                    else menu = MenuPage.MAIN
                }
                BackHandler(enabled = settingsPage != null && waitingKey == null && editLayout == null) {
                    settingsPage = settingsPage?.up
                }
            }
        }
    }

    /** A picked file read by [read] (which stops at the most it takes), or null if it can't be read. */
    private fun readPicked(uri: Uri, read: (InputStream) -> ByteArray?): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use(read)
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
                size = info?.second ?: 0, console = info?.first?.let(Console::ofName))
        open(entry)
    }

    /** The games folder picked: its permission kept (the old folder's given back), the list read from it. */
    private fun useFolder(tree: Uri) {
        try {
            contentResolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        library.folder?.let { old ->
            if (old != tree.toString() && old != backupSchedule.folder) try { // the backup folder still needs its read permission
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
        if (folder == null && unknown.isEmpty() && unsized.isEmpty()) {
            startBoxArt()
            return
        }
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
                startBoxArt()
            }
        }
    }

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
        thread(name = "box-picture") { // the picture (up to 50 MiB) and the game may be on a slow or remote provider
            val id = g.id.ifEmpty { readGame(Uri.parse(g.uri))?.let(GameId::of) }
            val ok = id != null && readUpTo(uri, 50 shl 20)?.let(BoxPicture::shrink)?.let { boxArt.own(id, it) } == true
            runOnUiThread {
                if (id != null && g.id.isEmpty()) {
                    library.played(g.copy(id = id))
                    library.save()
                    rows = library.rows()
                }
                when {
                    id == null -> toast("Can't open ${g.name}.")
                    !ok -> toast("That picture couldn't be read.")
                    else -> libraryRefresh++
                }
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

    /** The game's bytes (from a game file or a .zip), or null. */
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
        val bytes = exportBytes ?: run { // the app was restarted while the picker was open: the save is no longer here
            try {
                DocumentsContract.deleteDocument(contentResolver, uri) // the picker already made an empty file
            } catch (e: Exception) {
            }
            toast("The export didn't finish. Export the save again.")
            return
        }
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
        val bytes = readPicked(uri, SaveFiles::read) ?: run { toast("Could not read that file."); return }
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
    /** Reads the game off the UI thread (up to 32 MiB, maybe unzipped, maybe from a slow provider), then opens it;
     *  a spinner after 0.3 s; taps while it reads are ignored. */
    private fun open(entry: GameEntry, resumeAuto: Boolean = true) {
        if (opening) return
        opening = true
        stopBoxArt()
        val slow = Runnable { if (busy == null) busy = OPENING }
        handler.postDelayed(slow, 300)
        thread(name = "open-game") {
            val read: Any? = try {
                contentResolver.openInputStream(Uri.parse(entry.uri))?.use(RomFile::readNamed)
            } catch (e: RomFile.NotAGame) {
                e
            } catch (e: Exception) {
                null
            }
            val bytes: Any? = (read as? RomFile.Named)?.bytes ?: read
            val id = (bytes as? ByteArray)?.let(GameId::of)
            // The console: by the game's own bytes; else by the zip's inner name or what the list knows from the
            // file's name; a game nothing is known about is tried as a GBA game, as before there were others.
            val console = (bytes as? ByteArray)?.let { Console.of(it, (read as? RomFile.Named)?.zipName) }
                ?: entry.console ?: Console.GBA
            runOnUiThread {
                opening = false
                handler.removeCallbacks(slow)
                if (busy == OPENING) busy = null
                if (isDestroyed) return@runOnUiThread
                when {
                    bytes is RomFile.NotAGame -> message = NOT_A_GAME
                    bytes !is ByteArray || id == null -> message = "Can't open ${entry.name}."
                    else -> openRead(entry.copy(console = console), resumeAuto, bytes, id)
                }
            }
        }
    }

    private fun openRead(entry: GameEntry, resumeAuto: Boolean, bytes: ByteArray, gameId: String) {
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
        val console = entry.console ?: Console.GBA
        val loadError = NativeBridge.loadGame(rom, File(saves, "$gameId.sav").path, console.native)
        if (loadError != 0) { // (the previous game's session is already stopped and saved)
            message = loadErrorText(loadError)
            return
        }
        if (own == GameSettings.Game()) NativeBridge.cartSetup().let { gameSettings.remember(gameId, it[0], it[1]) }
        hardware = NativeBridge.cartHardware()
        manualLight = null
        val played = entry.copy(id = gameId, lastPlayed = System.currentTimeMillis(),
            console = Console.ofNative(NativeBridge.console()) ?: console)
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
        raPopups.clear()
        raView = raPopups.view()
        raSet = false
        raBoards = emptyList()
        openBoard = null
        hardcoreOn = false
        // RetroAchievements: GBA games only for now (17b) — a Game Boy game is not even identified.
        if (console == Console.GBA && ra.loadGame(rom)) waitForAchievements { game -> useAchievements(game); continueOpen(st, resumeAuto) }
        else continueOpen(st, resumeAuto)
    }

    /** The rest of opening a game, once RetroAchievements has answered (or there was nothing to ask). */
    private fun continueOpen(st: States, resumeAuto: Boolean) {
        if (resumeAuto && st.used(States.AUTO)) {
            if (hardcoreOn) {
                raPopups.note("Hardcore: started from your in-game save", SystemClock.uptimeMillis())
                raView = raPopups.view()
            } else {
                if (st.load(States.AUTO)) {
                    startGame()
                    return
                }
                toast("Couldn't resume where you left off; starting the game.")
            }
        }
        when (NativeBridge.saveStatus()) {
            NativeBridge.SAVE_EMPTY -> {
                importError = null
                askImport = true
            }
            NativeBridge.SAVE_MISFIT -> {
                startGame()
                toast("This game's save file doesn't fit it, so the game starts without it. The file is kept and goes into your backups.")
            }
            else -> startGame()
        }
    }

    /** Waits (on the UI thread, 50 ms at a time) for RetroAchievements to identify the game; a spinner after 0.3 s, up to 10 s. */
    private fun waitForAchievements(then: (Achievements.Game) -> Unit) {
        val start = SystemClock.uptimeMillis()
        val step = object : Runnable {
            override fun run() {
                val waited = SystemClock.uptimeMillis() - start
                val game = ra.gameStep(timedOut = waited >= RA_WAIT_MS)
                if (game == null) {
                    if (waited >= 300 && busy == null) busy = CHECKING
                    handler.postDelayed(this, 50)
                    return
                }
                if (busy == CHECKING) busy = null
                refreshRaUi()
                then(game)
            }
        }
        step.run()
    }

    private fun useAchievements(game: Achievements.Game) {
        val now = SystemClock.uptimeMillis()
        when (game) {
            is Achievements.Game.Set -> {
                raSet = true
                // Its badge pictures, downloaded now, so a pop-up shows the picture rather than the trophy placeholder.
                val urls = RaAchievement.list(NativeBridge.raAchievements()).map { it.unlockedBadgeUrl }
                thread(isDaemon = true, name = "ra-badges") { badges.prefetch(urls) }
                hardcoreOn = NativeBridge.raHardcoreActive()
                raPopups.note("${game.unlocked} of ${game.total} achievements" + if (hardcoreOn) " · Hardcore" else "", now)
            }
            Achievements.Game.Offline -> raPopups.note("RetroAchievements: offline", now)
            Achievements.Game.NoSet -> {}
        }
        raBoards = if (game is Achievements.Game.Offline) emptyList() else RaBoard.list(NativeBridge.raLeaderboards())
        raView = raPopups.view()
    }

    private fun importFrom(uri: Uri) {
        val bytes = readPicked(uri, SaveFiles::read)
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
        raPopups.clear()
        raView = raPopups.view()
        raSet = false
        raBoards = emptyList()
        openBoard = null
        hardcoreOn = false
        setFullScreen(false)
    }

    /** Back: the game pauses (sound, sensors, rumble too; the save file is written) behind the menu. */
    private fun openMenu() {
        if (hardcoreOn && NativeBridge.raCanPause() > 0) { // hardcore: no quick repeated pauses
            raPopups.note("Wait a few seconds to pause again in hardcore mode", SystemClock.uptimeMillis())
            raView = raPopups.view()
            return
        }
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
        if (hardcoreOn) return toast("Off in hardcore mode")
        val st = states ?: return
        if (!st.used(States.QUICK)) return toast("No quick save yet")
        NativeBridge.pause()
        val ok = st.load(States.QUICK)
        NativeBridge.resume()
        if (!ok) toast("This state can't be loaded (damaged, or made by an older version).")
    }

    /** The chosen imported Delta skin, or null (a built-in skin). */
    /** The console of the game being played (GBA with none): its picture's shape, its buttons, its layouts. */
    private val gameConsole: Console get() = if (playing) current?.console ?: Console.GBA else Console.GBA

    /** The imported (Delta) skin in use — never in a Game Boy game: those skins are made for the GBA (17b). */
    private fun activeSkin(): SkinFiles.Imported? =
        if (settings.skinImport == 0 || gameConsole != Console.GBA) null
        else importedSkins.firstOrNull { it.number == settings.skinImport }

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
            } catch (e: Throwable) { // the file can't be opened, or its pictures can't be decoded (out of memory)
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

    /** The moment the player left, for resuming it next time (the game must be paused). */
    private fun saveAuto() {
        if (playing) states?.save(States.AUTO)
    }

    private fun quitGame() {
        saveAuto()
        stopGame()
        refreshLibrary() // it is now first, with the picture of where it was left
        autoBackup() // the save and the Continue state are written by now
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
        NativeBridge.setRewinding(!hardcoreOn && all and KeyMap.REWIND != 0)
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
        if (hardcoreOn) { // hardcore: no cheats (the emulator refuses them too)
            NativeBridge.setCheats(arrayOf(), intArrayOf())
            return
        }
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

    /** The phone's name as the user set it, written into backups. */
    private fun phoneName(): String =
        android.provider.Settings.Global.getString(contentResolver, android.provider.Settings.Global.DEVICE_NAME) ?: android.os.Build.MODEL

    /** Settings → About → GitHub page: the browser (16); nothing is fetched by the app. */
    private fun openGithub() {
        try {
            startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(GITHUB_URL)))
        } catch (e: android.content.ActivityNotFoundException) {
            toast("No browser to open $GITHUB_URL")
        }
    }

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
                try { // the backup is made: a failed clean-up of old ones doesn't undo that
                    val all = f.list()
                    val old = BackupSchedule.toDelete(all.map { it.first }).toSet()
                    for ((name, uri) in all) if (name in old) f.delete(uri)
                } catch (e: Exception) {
                }
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

    /** The file's bytes, or null if it can't be read or is more than [max] bytes (not read whole). */
    private fun readUpTo(uri: Uri, max: Int): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use { Net.readUpTo(it, max) }
    } catch (e: Exception) {
        null
    }

    /** A setting changed (Settings screen, menu row): kept, and used at once where it can be. */
    private fun changeSettings(s: Settings) {
        if (s.hardcore != settings.hardcore) {
            if (s.hardcore && playing && raSet && ra.signIn == Achievements.SignIn.IN) {
                askHardcoreRestart = true // the switch stays off unless the user says yes
                return
            }
            if (!s.hardcore) hardcoreOn = false
        }
        settings = s
        s.save(settingsStore)
        fastSpeed = s.fastSpeed
        applySettings(s)
        if (playing) sendKeys() // a switched-on fast mode uses the new speed
        if (s.boxArt) {
            if (!playing) startBoxArt()
        } else stopBoxArt()
    }

    /** Picture and sound at once; rewind for the next game (the touch buttons read [settings] themselves). */
    private fun applySettings(s: Settings) {
        NativeBridge.setPicture(s.size, s.smooth)
        NativeBridge.setBackground(if (s.skinImport != 0) 0xFF000000.toInt() else Skins.at(s.skin).body)
        NativeBridge.setSound(s.sound, s.soundBursts)
        NativeBridge.setRewindBudget(s.rewindBudget)
        NativeBridge.raSetHardcore(s.hardcore)
        raPopups.leaderboardNotes = s.leaderboardNotes
        raView = raPopups.view()
    }

    private fun refreshRaUi() {
        raUi = RaUi(ra.signIn, ra.name, ra.error)
    }

    /** While a sign-in is on its way: picks up the answer (every 200 ms). */
    private fun watchSignIn() {
        refreshRaUi()
        if (ra.signIn != Achievements.SignIn.BUSY) return
        handler.postDelayed({
            ra.update()
            watchSignIn()
        }, 200)
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
        val bytes = readPicked(uri, Bios::read) ?: run { toast("Could not read that file."); return }
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
        stopBoxArt()
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

private const val RA_WAIT_MS = 10_000L // the longest a game waits for RetroAchievements before it starts (14)
private const val CHECKING = "Checking RetroAchievements"
private const val OPENING = "Opening the game"
