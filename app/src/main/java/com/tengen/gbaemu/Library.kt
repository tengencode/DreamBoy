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

    fun save(): Boolean = file.writeSafely(toJson(folder, games).toByteArray())

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
