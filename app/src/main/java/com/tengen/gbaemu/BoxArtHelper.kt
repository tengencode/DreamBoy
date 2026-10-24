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
        file.writeSafely("#time\t$time\n$body".toByteArray()) // if not kept: downloaded again next time
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
