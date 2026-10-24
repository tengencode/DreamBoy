package com.tengen.gbaemu

/** libretro's `.cht` cheat files (10b): which of them belong to a game, by the game's name. Pure: tested on the PC. */
object CheatSources {
    const val NONE_FOUND = "No cheat files found for this game."
    const val NO_INTERNET = "Couldn't reach the internet."

    /** The `.cht` file names in GitHub's reply listing a folder; null if the reply isn't such a list. */
    fun fromListing(json: String): List<String>? = try {
        val all = Json.Reader(json).run { value().also { end() } } as? List<*>
        all?.mapNotNull { item ->
            val m = item as? Map<*, *> ?: return@mapNotNull null
            (m["name"] as? String)?.takeIf { m["type"] == "file" && it.endsWith(".cht") }
        }
    } catch (e: Exception) {
        null
    }

    /**
     * The files made for the game called [name], as (label, file name), A-Z by label: `<name>.cht` ("Cheats") or
     * `<name> (<label>).cht` with exactly one more bracket part; the name as is or as libretro writes picture names.
     */
    fun forGame(files: List<String>, name: String): List<Pair<String, String>> {
        val bases = listOf(name, GameNames.pictureName(name)).distinct()
        return files.mapNotNull { f ->
            val stem = f.removeSuffix(".cht")
            bases.firstNotNullOfOrNull { b ->
                when {
                    stem == b -> "Cheats"
                    stem.startsWith("$b (") && stem.endsWith(")") ->
                        stem.substring(b.length + 2, stem.length - 1).takeIf { '(' !in it && ')' !in it }
                    else -> null
                }
            }?.let { it to f }
        }.sortedBy { it.first }
    }
}
