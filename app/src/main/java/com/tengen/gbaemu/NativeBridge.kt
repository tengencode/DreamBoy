package com.tengen.gbaemu

import android.view.Surface
import java.nio.ByteBuffer

/** The C side (platform/android/jni_bridge.c). One game session at a time; call from one thread. */
object NativeBridge {
    init {
        System.loadLibrary("gbaemu")
    }

    /**
     * Creates the emulator with this ROM (a direct buffer; the core copies it) and loads its save file
     * from savePath if there is one (null: no save file). The game's saves go to that file ~1 s after
     * the game saves, and on pause/stop. 0 = OK, else a gba_err_t. Stops (and saves) a previous game.
     */
    external fun loadRom(rom: ByteBuffer, savePath: String?): Int

    /**
     * As [loadRom], saying which console the game is for (CONSOLE_*, as the app worked out from the file's bytes
     * and name); any other value: told from the ROM's own header. ERR_UNSUPPORTED: a Game Boy cartridge chip the
     * emulator does not have.
     */
    external fun loadGame(rom: ByteBuffer, savePath: String?, console: Int): Int

    /** The loaded game's console (CONSOLE_*), or -1 without a game. */
    external fun console(): Int

    /** The loaded game's picture: 240 x 160, or 160 x 144 for a Game Boy game (240 x 160 without a game). */
    external fun frameWidth(): Int
    external fun frameHeight(): Int

    const val CONSOLE_GBA = 0
    const val CONSOLE_GB = 1
    const val CONSOLE_GBC = 2
    const val ERR_UNSUPPORTED = 100

    fun loadRom(rom: ByteBuffer): Int = loadRom(rom, null)

    /** The BIOS for the next games: a 16 KiB BIOS file's bytes, or null = the built-in one. 0, or ERR_BIOS_SIZE. */
    external fun setBios(bytes: ByteArray?): Int

    /** Bytes for the next games' rewind snapshots (0 = rewind off). */
    external fun setRewindBudget(bytes: Long)

    /** The next loadRom only: a forced save type (a gba_save_type_t) and clock (0 none, 1 one); -1 = detect. */
    external fun setGameOverrides(saveType: Int, clock: Int)

    /** Sound on/off and AAudio's buffer in bursts (2, 4, 8): at once, and for the next games. */
    external fun setSound(on: Boolean, bursts: Int)

    /** How the picture fills the screen (Settings.SIZE_*) and whether pixels are smoothed: at once. */
    external fun setPicture(fitMode: Int, smooth: Boolean)

    /** The colour round the game picture (ARGB: the skin's body, 8b), from the next frame on. */
    external fun setBackground(argb: Int)

    /** After loadRom: [save type (gba_save_type_t), HW_* extras], detected or forced; -1s without a game. */
    external fun cartSetup(): IntArray

    /** After loadRom: one of the SAVE_* values below. */
    external fun saveStatus(): Int

    /** Before start(): puts a .sav file's bytes in and writes the save file. 0 = OK, else a gba_err_t (nothing changed). */
    external fun importSave(bytes: ByteArray): Int

    /** True once after a save-file write failed. */
    external fun saveError(): Boolean

    /** Save states — only while paused (after pause()) or before start(). Writes the whole machine to path. */
    external fun saveState(path: String): Boolean

    /** 0 = loaded; else a gba_err_t (GBA_ERR_STATE = 5: another game, another version, damaged, or not paused). */
    external fun loadState(path: String): Int

    /** The current picture, frameWidth() x frameHeight() BGR555 pixels (only while paused), or null. */
    external fun frameBgr555(): ShortArray?

    const val ERR_BIOS_SIZE = 2
    const val ERR_STATE = 5
    const val ERR_CHEAT = 6

    /** Reads a cheat's text without a game: [the type it reads as (CHEAT_*), the first bad line (0 = none)]. */
    external fun checkCheat(text: String, type: Int): IntArray

    /** The switched-on cheats (only while paused or before start()): 0, or ERR_CHEAT (then none are on). */
    external fun setCheats(texts: Array<String>, types: IntArray): Int

    const val CHEAT_AUTO = 0
    const val CHEAT_ACTION_REPLAY = 1
    const val CHEAT_GAMESHARK = 2
    const val CHEAT_CODEBREAKER = 3
    const val CHEAT_RAW = 4

    /** Cartridge sensors, applied at the next frame: tilt x/y and turn ±32767 (turn > 0 = clockwise). */
    external fun setMotion(tiltX: Int, tiltY: Int, turn: Int)

    /** The solar sensor's light: 0 = dark .. 255 = full sun. */
    external fun setLight(level: Int)

    /** The loaded cartridge's extras: HW_* bits below. */
    /** Grey Game Boy games' colours (17c; [Looks]): the running game from its next frame, and every game opened after. */
    external fun setLook(look: Int)

    /** A look's twelve colours: the background's four, then each sprite palette's (15 bits, red lowest). */
    external fun lookColours(look: Int): ShortArray

    external fun cartHardware(): Int

    /** True while the cartridge's rumble motor runs (as of the last frame). */
    external fun rumbleActive(): Boolean

    const val HW_RTC = 1
    const val HW_SOLAR = 2
    const val HW_GYRO = 4
    const val HW_RUMBLE = 8
    const val HW_TILT = 16

    const val SAVE_NONE = 0 // no save chip: nothing to offer
    const val SAVE_LOADED = 1 // the save file was loaded
    const val SAVE_EMPTY = 2 // a save chip and no save file yet: offer an import
    const val SAVE_MISFIT = 3 // the save file does not fit the chip: not loaded, kept as `<id>.sav.misfit` (and in backups)

    /** Starts the emulation and render threads for the loaded ROM. */
    external fun start()

    /** Stops both threads and frees the emulator. */
    external fun stop()

    /** Returns once the game has stopped and its save is written (at most 1 s). */
    external fun pause()

    external fun resume()

    /** The game's window appeared/changed (or went away: null). False if OpenGL ES 3 cannot draw into it. */
    external fun setSurface(surface: Surface?): Boolean

    /** Held GBA buttons (KeyMap bits). */
    external fun setKeys(pressed: Int)

    /** Fast-forward: 0 = normal, 2/4/8 = that many times faster (sound sped up), FF_MAX = flat out, silent. */
    external fun setFastForward(speed: Int)

    /** While true the game runs backwards (one snapshot of 4 frames per 1/60 s, silent); false plays on from there. */
    external fun setRewinding(on: Boolean)

    const val FF_MAX = -1

    // RetroAchievements (14): platform/ra/ra.h through platform/android/jni_ra.c. Text is UTF-8 bytes.
    const val RA_NOTHING = 0
    const val RA_BUSY = 1
    const val RA_OK = 2
    const val RA_FAILED = 3

    /** The next request of rcheevos' (id, url, post or null, content type or null), or null after [timeoutMs]. */
    external fun raWaitRequest(timeoutMs: Int): Array<String?>?
    external fun raReply(id: Int, status: Int, body: ByteArray?)
    external fun raLoginPassword(user: ByteArray, password: ByteArray)
    external fun raLoginToken(user: ByteArray, token: ByteArray)
    external fun raLogout()
    /** [status (RA_*), rcheevos' code]. */
    external fun raLoginStatus(): IntArray
    external fun raLoginMessage(): ByteArray
    /** [name, token], or null when signed out. */
    external fun raUser(): Array<ByteArray>?
    /** The ROM given to [loadRom]: its MD5 identifies the game. */
    external fun raLoadGame(rom: ByteBuffer)
    external fun raUnloadGame()
    /** While waiting for [raLoadGame]: lets the load finish. */
    external fun raPoll()
    external fun raGameStatus(): IntArray
    external fun raSetHardcore(on: Boolean)
    /** The hardcore limits apply now (hardcore on, signed in, the game has achievements). */
    external fun raHardcoreActive(): Boolean
    /** 0: may pause; else frames to play first. */
    external fun raCanPause(): Int
    /** [kind, id, points, title, description, progress, badge url, rank, entries, best], or null. */
    external fun raTakeEvent(): Array<ByteArray>?
    /** [unlocked, total, points unlocked, points total], or null. */
    external fun raSummary(): IntArray?
    external fun raGameTitle(): ByteArray
    /** 7 texts per achievement, unlocked first: id, points, unlocked 1/0, title, description, progress, badge url. */
    external fun raAchievements(): Array<ByteArray>
    external fun raUserAgentClause(): String

    // Leaderboards (15)
    external fun raHasLeaderboards(): Boolean
    /** 4 texts per board: id, lower is better 1/0, title, description. */
    external fun raLeaderboards(): Array<ByteArray>
    /** Asks for a board's scores (the top 10 + the rows around the user); only the newest counts. */
    external fun raFetchEntries(board: Int)
    /** [status (RA_*), rcheevos' code, total players, on the board 1/0]. */
    external fun raEntriesStatus(): IntArray
    external fun raEntriesMessage(): ByteArray
    /** 5 texts per row, best first: rank, the user's 1/0, a gap before it 1/0, name, score. */
    external fun raEntries(): Array<ByteArray>

    // ---- test-only ----

    /** A word of the game's memory (only while paused or before start). */
    external fun debugPeek32(addr: Int): Int

    /** Frames the emulation thread has run since loadRom. */
    external fun debugFrameCount(): Long

    /** Sound since loadRom: [active 1/0, sample rate, frames per burst, played, silence-filled, loud, reopens]. */
    external fun debugAudioStats(): LongArray

    /** Before start(): false = no sound stream, the clock paces the game. */
    external fun debugSetAudioEnabled(enabled: Boolean)

    /** As if the sound device went away (headphones unplugged): the stream must reopen. */
    external fun debugSimulateAudioDisconnect()

    /** The stream silently stops taking sound: the watchdog must reopen it (after its 2 s start-up grace). */
    external fun debugSimulateStuckStream()

    /** The core's save bytes (call while paused), or null without a game. */
    external fun debugSaveRam(): ByteArray?

    /** The sensor values last applied to the core: [tilt x, tilt y, turn, light]. */
    external fun debugMotionSeen(): IntArray

    /** Buttons the game was given in any frame since the last call (KeyMap bits). */
    external fun debugKeysSeen(): Int

    /** Runs a ROM headless for n frames on this thread: "<frame hash> <audio hash>" like the PC test runner. */
    external fun debugRunFrames(rom: ByteBuffer, frames: Int): String?

    /** The same, but at frame stateAt it carries on in a new machine loaded from a save state (null: failed). */
    external fun debugRunFramesViaState(rom: ByteBuffer, frames: Int, stateAt: Int): String?

    /** The same, but at frame rewindAt it steps back rewindSteps snapshots (4 frames apart) and plays on (null: failed). */
    external fun debugRunFramesViaRewind(rom: ByteBuffer, frames: Int, rewindAt: Int, rewindSteps: Int): String?

    /** Rewind since start: [snapshots kept, their packed bytes, steps back taken]. */
    external fun debugRewindStats(): LongArray

    /** Draws one 240x160 (or 160x144: told by the array's size) BGR555 frame offscreen; RGBA pixels (r | g<<8 | b<<16 | a<<24), top row first. */
    external fun debugDrawFrame(pixels: ShortArray, width: Int, height: Int): IntArray?

    /** As debugDrawFrame, with a picture size (Settings.SIZE_*) and sharp or smooth pixels. */
    external fun debugDrawFrameWith(pixels: ShortArray, width: Int, height: Int, fitMode: Int, smooth: Boolean): IntArray?

    /** As debugDrawFrameWith, round the picture the colour [background] (ARGB) instead of black. */
    external fun debugDrawFrameOn(pixels: ShortArray, width: Int, height: Int, fitMode: Int, smooth: Boolean,
                                  background: Int): IntArray?
}
