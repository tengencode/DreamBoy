package com.tengen.gbaemu

import android.graphics.Bitmap
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onAllNodesWithContentDescription
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTouchInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** The screens of milestone 8a's new look, 8b's skins and 9's Delta skins: what they show and what tapping them does. */
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
                GameMenu("Sun Quest", 4, onFastSpeed = { picked = it }, onResume = { resumed = true }, onSave = {}, onLoad = {},
                    onCheats = {}, onSettings = {}, onRestart = {}, onQuit = {})
            }
        }
        for (label in listOf("Sun Quest", "Resume", "Save state", "Load state", "Cheats", "Settings", "Restart", "Quit")) {
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

    /** Settings at [start], following onPage like the app does; [opened] counts GitHub-page taps. */
    private fun settingsAt(start: SettingsPage, onGithub: () -> Unit = {}) {
        val page = mutableStateOf(start)
        compose.setContent {
            AppTheme {
                SettingsScreen(page.value, Settings(), KeyBindings(MemoryStore()), 0, "Built-in",
                    onPage = { page.value = it }, onBack = { page.value = page.value.up ?: SettingsPage.MAIN }, onChange = {},
                    onPickKey = { _, _ -> }, onResetKeys = {}, onChooseBios = {}, onBuiltInBios = {},
                    version = "1.0.0", onOpenGithub = onGithub)
            }
        }
    }

    @Test
    fun aboutShowsTheNameVersionAndLinks() {
        var github = 0
        settingsAt(SettingsPage.MAIN) { github++ }
        compose.onNodeWithText("About DreamBoy").performScrollTo().performClick()
        compose.onNodeWithText("Version 1.0.0").assertIsDisplayed()
        for (row in listOf("GitHub page", "Open-source licences", "Privacy policy")) compose.onNodeWithText(row).assertIsDisplayed()
        compose.onNodeWithText("GitHub page").performClick()
        assertEquals(1, github)
    }

    @Test
    fun licencesListEveryPartAndOpenItsText() {
        settingsAt(SettingsPage.LICENCES)
        compose.onNodeWithText("Jetpack Compose", substring = true).assertExists()
        compose.onNodeWithText("zlib-style").assertExists() // the MD5 code inside rcheevos
        compose.onNodeWithText("rcheevos 12.5.0 (RetroAchievements)").performScrollTo().performClick()
        compose.onNodeWithText("MIT License", substring = true).assertExists()
    }

    @Test
    fun privacyPolicyIsShown() {
        settingsAt(SettingsPage.PRIVACY)
        compose.onAllNodesWithText("RetroAchievements", substring = true).fetchSemanticsNodes().let { assertEquals(true, it.isNotEmpty()) }
        compose.onAllNodesWithText("## ", substring = true).assertCountEquals(0) // headings shown as headings, not markdown
        compose.onAllNodesWithText("**", substring = true).assertCountEquals(0)
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
        val older = GameEntry("a", "u", "Dungeon Run", lastPlayed = 100)
        val newest = GameEntry("b", "u", "Green Quest", lastPlayed = 300)
        val never = GameEntry("c", "u", "Micro Party")
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
        compose.onNode(hasText("Green Quest")).assertIsDisplayed()
        compose.onNodeWithText("Continue").assertIsDisplayed()
        compose.onNodeWithContentDescription("Dungeon Run: more").performClick()
        assertEquals(older, held)
        compose.onNodeWithTag("continue").performClick()
        compose.waitForIdle() // the cartridge goes into the slot first, then the game opens
        assertEquals(newest, played)
    }

    @Test
    fun importedSkinsArePickedImportedAndRemovedAndTheirLayoutRowsAreOff() {
        var changed = Settings()
        var imported = false
        var removed = 0
        val edited = ArrayList<Boolean>()
        val page = mutableStateOf(SettingsPage.SKIN)
        val skins = listOf(ImportedSkin(2, "Bumble", null, upright = true, sideways = false))
        compose.setContent {
            AppTheme {
                SettingsScreen(page.value, Settings(skin = 5, skinImport = 2), KeyBindings(MemoryStore()), 0, "Built-in",
                    onPage = {}, onBack = {}, onChange = { changed = it }, onPickKey = { _, _ -> }, onResetKeys = {},
                    onChooseBios = {}, onBuiltInBios = {}, onEditLayout = { side, _ -> edited += side }, importedSkins = skins,
                    onImportSkin = { imported = true }, onRemoveSkin = { removed = it })
            }
        }
        compose.onNodeWithText("Bumble").performScrollTo().performClick()
        assertEquals(2, changed.skinImport)
        compose.onNodeWithText("Import").performScrollTo().performClick()
        assertEquals(true, imported)
        compose.onNodeWithText("Bumble").performScrollTo().performTouchInput { longClick() }
        compose.onNodeWithText("Remove Bumble?").assertIsDisplayed()
        compose.onNodeWithText("Remove").performClick()
        assertEquals(2, removed)
        compose.onNodeWithText("Ice").performScrollTo().performClick()
        assertEquals(1, changed.skin) // a built-in skin: the imported one is no longer chosen
        assertEquals(0, changed.skinImport)
        page.value = SettingsPage.TOUCH
        compose.onNodeWithText("GBA: Bumble · Game Boy: ${Skins.at(5).name}").assertIsDisplayed() // the Skin row names each family's skin
        compose.onNodeWithText("This skin sets its own layout").assertIsDisplayed() // upright: the skin covers it
        compose.onNodeWithText("Edit upright layout").performClick()
        compose.onNodeWithText("Edit sideways layout").performClick()
        assertEquals(listOf(true), edited) // only sideways opens
    }

    @Test
    fun eachConsoleFamilyHasItsOwnImportedSkinOnTheSkinAndTouchPages() {
        var changed = Settings()
        val edited = ArrayList<Pair<Boolean, Boolean>>()
        val page = mutableStateOf(SettingsPage.SKIN)
        val skins = listOf(ImportedSkin(2, "Bumble", null, upright = true, sideways = true),
            ImportedSkin(3, "Pocket", null, upright = true, sideways = false, family = "gb"))
        compose.setContent {
            AppTheme {
                SettingsScreen(page.value, Settings(skinImportGb = 3), KeyBindings(MemoryStore()), 0, "Built-in",
                    onPage = {}, onBack = {}, onChange = { changed = it }, onPickKey = { _, _ -> }, onResetKeys = {},
                    onChooseBios = {}, onBuiltInBios = {}, onEditLayout = { side, gb -> edited += side to gb }, importedSkins = skins)
            }
        }
        compose.onNodeWithText("GBA").performScrollTo().assertIsDisplayed() // each imported tile carries its console mark
        compose.onNodeWithText("GB").performScrollTo().assertIsDisplayed()
        compose.onNodeWithText("Bumble").performScrollTo().performClick() // a GBA skin: the Game Boy choice stays
        assertEquals(Settings(skinImport = 2, skinImportGb = 3), changed)
        compose.onNodeWithText("Pocket").performScrollTo().performClick()
        assertEquals(Settings(skinImportGb = 3), changed)
        compose.onNodeWithText("Ice").performScrollTo().performClick() // a built-in skin: back from both
        assertEquals(Settings(skin = 1), changed)
        page.value = SettingsPage.TOUCH
        compose.onNodeWithText("GBA: Plain · Game Boy: Pocket").assertIsDisplayed()
        for (row in listOf("Edit upright layout", "Edit sideways layout", "Edit Game Boy upright layout", "Edit Game Boy sideways layout")) {
            compose.onNodeWithText(row).performScrollTo().performClick()
        }
        assertEquals(listOf(false to false, true to false, true to true), edited) // only the Game Boy skin's upright row is off
    }

    @Test
    fun theLoadPageOffersTheQuickSlotFirstAndTheSavePageDoesNot() {
        val dir = File(InstrumentationRegistry.getInstrumentation().targetContext.cacheDir, "quickslot").apply { deleteRecursively() }
        val states = States(dir, "game")
        states.dir.mkdirs()
        states.state(States.QUICK).writeText("q")
        states.state(2).writeText("s")
        var picked = -1
        val saving = mutableStateOf(false)
        compose.setContent { AppTheme { SlotGrid(saving.value, states, 0, onPick = { picked = it }, onBack = {}) } }
        compose.onNodeWithContentDescription("Slot 2").assertIsDisplayed()
        compose.onNodeWithContentDescription("Quick").performClick()
        assertEquals(States.QUICK, picked)
        saving.value = true
        compose.onNodeWithContentDescription("Quick").assertDoesNotExist()
        compose.onNodeWithContentDescription("Slot 10").performScrollTo().assertIsDisplayed()
    }

    @Test
    fun theCheatPanelImportsAFileAndRemovesAllAfterAQuestion() {
        var imported = false
        var removed = 0
        val list = mutableStateOf(listOf(Cheat("A", 0, "0200A000:63", false), Cheat("B", 0, "0200A001:01", true)))
        compose.setContent {
            AppTheme {
                CheatPanel("Sun Quest", list.value, onToggle = { _, _ -> }, onEdit = {}, onAdd = {}, onBack = {},
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

    @Test
    fun smallCardsShowTheirBoxAndTheContinueCardDoesNot() {
        val box = Bitmap.createBitmap(40, 40, Bitmap.Config.ARGB_8888)
        val rows = listOf(GameEntry("a", "u:a", "Sun Quest", "aaaaaaaa", lastPlayed = 5), GameEntry("b", "u:b", "Micro Party", "bbbbbbbb"),
            GameEntry("c", "u:c", "Homebrew", "cccccccc"))
        compose.setContent {
            AppTheme {
                LibraryScreen(rows, "GBA", scanning = false, message = null, refresh = 0, picture = { null },
                    box = { g -> if (g.key == "c") null else box }, onPlay = {}, onHold = {}, onOpenGame = {},
                    onChangeFolder = {}, onSettings = {})
            }
        }
        compose.onNodeWithText("Micro Party").assertIsDisplayed()
        compose.onAllNodesWithContentDescription("Box art").assertCountEquals(1) // Micro Party only
    }

    @Test
    fun theHoldMenuOffersChooseBoxArtAndRemoveOnlyWithABox() {
        var chosen = 0
        var removed = 0
        val hasBox = mutableStateOf(false)
        compose.setContent {
            AppTheme {
                GameHoldMenu(GameEntry("a", "u:a", "Sun Quest"), onStartFresh = {}, onImport = {}, onExport = {},
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
                CheatPanel("Sun Quest", emptyList(), onToggle = { _, _ -> }, onEdit = {}, onAdd = {}, onBack = {},
                    onFindOnline = { asked++ })
                state.value?.let { OnlineCheatsDialog(it, onPick = { f -> picked = f }, onClose = { state.value = null }) }
            }
        }
        compose.onNodeWithText("Find cheats online").performClick()
        assertEquals(1, asked)
        state.value = OnlineCheats.Looking
        compose.onNodeWithText("Looking for cheat files").assertIsDisplayed()
        state.value = OnlineCheats.Found(listOf("Code Breaker" to "Sun Quest (USA) (Code Breaker).cht", "GameShark" to "g.cht"))
        compose.onNodeWithText("Code Breaker").performClick()
        assertEquals("Sun Quest (USA) (Code Breaker).cht", picked)
        state.value = OnlineCheats.Message(CheatSources.NONE_FOUND)
        compose.onNodeWithText("No cheat files found for this game.").assertIsDisplayed()
        compose.onNodeWithText("Close").performClick()
        compose.onNodeWithText("Cheats online").assertDoesNotExist()
    }

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

    @Test
    fun theThemeFollowsDarkModeAndTheSkinsAccent() {
        val dark = mutableStateOf(false)
        val seed = mutableStateOf(AccentColors.DEFAULT_SEED)
        var background = Color.Unspecified
        var primary = Color.Unspecified
        compose.setContent {
            AppTheme(dark = dark.value, accentSeed = seed.value) {
                background = MaterialTheme.colorScheme.background
                primary = MaterialTheme.colorScheme.primary
            }
        }
        compose.waitForIdle()
        assertEquals(Color(Palette.LIGHT.plastic), background)
        assertEquals(Color(AccentColors.light(AccentColors.DEFAULT_SEED).color), primary)
        dark.value = true
        seed.value = AccentColors.seedFor("orange")
        compose.waitForIdle()
        assertEquals(Color(Palette.DARK.plastic), background)
        assertEquals(Color(AccentColors.dark(AccentColors.seedFor("orange")).color), primary)
    }

    @Test
    fun theFirstStartShowsAnEmptySlotAndItsButtons() {
        var chose = 0
        var opened = 0
        var settings = 0
        compose.setContent {
            AppTheme { FirstStartScreen(false, null, onChooseFolder = { chose++ }, onOpenGame = { opened++ }, onSettings = { settings++ }) }
        }
        // The gear must be reachable before a folder is chosen (16: a new DreamBoy restores a backup from Settings).
        compose.onNodeWithContentDescription("Settings").performClick()
        assertEquals(1, settings)
        compose.onNodeWithText("No cartridge").assertIsDisplayed()
        compose.onNodeWithText("Choose games folder").performClick()
        compose.onNodeWithText("Open one game").performClick()
        assertEquals(1, chose)
        assertEquals(1, opened)
    }

    @Test
    fun withNothingPlayedTheSlotIsEmptyAndACartridgePlaysAfterGoingIn() {
        var played: GameEntry? = null
        val game = GameEntry("c", "u", "Micro Party")
        compose.setContent {
            AppTheme {
                LibraryScreen(listOf(game), "GBA", scanning = false, message = null, refresh = 0, picture = { null },
                    onPlay = { played = it }, onHold = {}, onOpenGame = {}, onChangeFolder = {}, onSettings = {})
            }
        }
        compose.onNodeWithText("Pick a game below to start").assertIsDisplayed()
        compose.onNodeWithTag("game").performClick()
        compose.waitForIdle()
        assertEquals(game, played)
    }

    @Test
    fun anEmptyFolderShowsAnEmptySlotAndChangeFolder() {
        var changed = 0
        compose.setContent {
            AppTheme {
                LibraryScreen(emptyList(), "GBA", scanning = false, message = null, refresh = 0, picture = { null },
                    onPlay = {}, onHold = {}, onOpenGame = {}, onChangeFolder = { changed++ }, onSettings = {})
            }
        }
        compose.onNodeWithText("No games in GBA yet").assertIsDisplayed()
        compose.onNodeWithText("Change folder").performClick()
        assertEquals(1, changed)
    }

    @Test
    fun thePauseMenuHasALeaderboardsRowOnlyForAGameWithBoards() {
        var opened = false
        compose.setContent {
            AppTheme {
                GameMenu("Sun Quest", 4, onFastSpeed = {}, onResume = {}, onSave = {}, onLoad = {}, onCheats = {}, onSettings = {},
                    onRestart = {}, onQuit = {}, onLeaderboards = { opened = true })
            }
        }
        compose.onNodeWithText("Leaderboards").performClick()
        assertEquals(true, opened)
    }

    @Test
    fun theLeaderboardsPageListsBoardsThenAScoreTableWithTheUsersRowMarked() {
        val open = mutableStateOf<RaBoard?>(null)
        val boards = listOf(RaBoard(11, true, "Sprint", "Finish fast"), RaBoard(12, false, "Survival", "Last long"))
        val rows = (1..10).map { RaEntry(it, false, false, "Player$it", "0:0$it.00".take(7)) } +
            RaEntry(17, true, true, "Tester", "0:12.00")
        compose.setContent {
            AppTheme {
                LeaderboardsPanel("Demo Set", boards, hardcore = false, open = open.value,
                    scores = if (open.value == null) null else BoardScores.Loaded(rows, 30, true),
                    onOpen = { open.value = it }, onRetry = {}, onBack = { open.value = null })
            }
        }
        compose.onNodeWithText("Scores are only sent in Hardcore mode").assertIsDisplayed()
        compose.onNodeWithText("Sprint").performClick()
        compose.onNodeWithText(keepTogether("30 players")).assertIsDisplayed() // shown with a no-break space
        compose.onNodeWithContentDescription("You: rank 17, Tester, 0:12.00").performScrollTo().assertIsDisplayed()
        compose.onNodeWithContentDescription("Ranks 11 to 16 not shown").assertExists()
    }

    @Test
    fun aBoardThatFailedOffersTryAgainAndAnEmptyOneSaysNoScoresYet() {
        var tries = 0
        val scores = mutableStateOf<BoardScores>(BoardScores.Failed("Couldn't load scores — no connection"))
        val board = RaBoard(12, false, "Survival", "Last long")
        compose.setContent {
            AppTheme {
                LeaderboardsPanel("Demo Set", listOf(board), hardcore = true, open = board, scores = scores.value,
                    onOpen = {}, onRetry = { tries++ }, onBack = {})
            }
        }
        compose.onNodeWithText("Couldn't load scores — no connection").assertIsDisplayed()
        compose.onNodeWithText("Try again").performClick()
        assertEquals(1, tries)
        scores.value = BoardScores.Loaded(emptyList(), 0, false)
        compose.onNodeWithText("No scores yet").assertIsDisplayed()
    }
}
