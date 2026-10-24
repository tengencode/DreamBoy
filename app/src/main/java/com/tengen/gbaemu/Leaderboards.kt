package com.tengen.gbaemu

/** One of the game's leaderboards (15), from NativeBridge.raLeaderboards. */
data class RaBoard(val id: Int, val lowerIsBetter: Boolean, val title: String, val description: String) {
    companion object {
        fun list(parts: Array<ByteArray>): List<RaBoard> = parts.map { String(it, Charsets.UTF_8) }.chunked(4)
            .filter { it.size == 4 }
            .map { p -> RaBoard(p[0].toIntOrNull() ?: 0, p[1] == "1", p[2], p[3]) }
    }
}

/** A row of a board's scores (NativeBridge.raEntries). [gapBefore]: places are missing above it. */
data class RaEntry(val rank: Int, val you: Boolean, val gapBefore: Boolean, val name: String, val score: String) {
    companion object {
        fun list(parts: Array<ByteArray>): List<RaEntry> = parts.map { String(it, Charsets.UTF_8) }.chunked(5)
            .filter { it.size == 5 }
            .map { p -> RaEntry(p[0].toIntOrNull() ?: 0, p[1] == "1", p[2] == "1", p[3], p[4]) }
    }
}

/** What a board's page shows (15). */
sealed interface BoardScores {
    data object Loading : BoardScores
    data class Failed(val message: String) : BoardScores
    data class Loaded(val rows: List<RaEntry>, val total: Int, val onBoard: Boolean) : BoardScores

    companion object {
        const val RC_NO_RESPONSE = -32 // rcheevos: no answer (no internet)

        /** From NativeBridge.raEntriesStatus' [status, code, total, on the board], the message and the rows (read only when needed). */
        fun of(status: IntArray, message: () -> String, rows: () -> List<RaEntry>): BoardScores = when (status[0]) {
            NativeBridge.RA_OK -> Loaded(rows(), status[2], status[3] == 1)
            NativeBridge.RA_FAILED ->
                if (status[1] == RC_NO_RESPONSE) Failed("Couldn't load scores — no connection")
                else Failed(message().let { if (it.isEmpty()) "Couldn't load scores" else "Couldn't load scores: $it" })
            else -> Loading
        }
    }
}

/** The score table's gap between rank [above] and rank [below]: "Ranks 11 to 14", or "Rank 11" for one. */
fun gapLabel(above: Int, below: Int): String {
    fun n(v: Int) = String.format(java.util.Locale.US, "%,d", v)
    val first = above + 1
    val last = below - 1
    return if (last <= first) "Rank ${n(first)}" else "Ranks ${n(first)} to ${n(last)}"
}

/**
 * A line of leaderboard text that wraps cleanly: no break before a " · " separator, and none between a number and
 * its word ("1,204 players", "Score 0:10.00", "best 0:09.50").
 */
fun keepTogether(s: String): String = s
    .replace(" · ", "\u00A0· ")
    .replace(Regex("""(\d) (players?)"""), "$1\u00A0$2")
    .replace(Regex("""\b(Score|best) """), "$1\u00A0")
