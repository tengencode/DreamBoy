package com.tengen.gbaemu

/** One of the glue's events (platform/ra/ra.h ra_event_t), from NativeBridge.raTakeEvent. */
data class RaEvent(val kind: Int, val id: Int, val points: Int, val title: String, val description: String,
                   val progress: String, val badgeUrl: String, val rank: Int = 0, val entries: Int = 0, val best: String = "") {
    companion object {
        const val UNLOCKED = 1
        const val PROGRESS = 2
        const val PROGRESS_HIDE = 3
        const val CHALLENGE_SHOW = 4
        const val CHALLENGE_HIDE = 5
        const val MASTERED = 6
        const val SERVER_ERROR = 7
        const val OFFLINE = 8
        const val ONLINE = 9
        const val RESET = 10
        // leaderboards (15)
        const val LB_STARTED = 11
        const val LB_FAILED = 12
        const val LB_SUBMITTED = 13
        const val LB_SCOREBOARD = 14
        const val LB_ERROR = 15
        const val TRACKER_SHOW = 16
        const val TRACKER_UPDATE = 17
        const val TRACKER_HIDE = 18

        /** raTakeEvent's 10 texts: kind, id, points, title, description, progress, badge url, rank, entries, best. */
        fun of(parts: Array<ByteArray>?): RaEvent? {
            if (parts == null || parts.size != 10) return null
            val p = parts.map { String(it, Charsets.UTF_8) }
            val kind = p[0].toIntOrNull() ?: return null
            return RaEvent(kind, p[1].toIntOrNull() ?: 0, p[2].toIntOrNull() ?: 0, p[3], p[4], p[5], p[6],
                p[7].toIntOrNull() ?: 0, p[8].toIntOrNull() ?: 0, p[9])
        }
    }
}

/** One achievement of the pause menu's list. */
data class RaAchievement(val id: Int, val points: Int, val unlocked: Boolean, val title: String, val description: String,
                         val progress: String, val badgeUrl: String) {
    /** The coloured picture the unlock pop-up shows: RetroAchievements names the grey one "<name>_lock.png". */
    val unlockedBadgeUrl: String get() = if (badgeUrl.endsWith("_lock.png")) badgeUrl.removeSuffix("_lock.png") + ".png" else badgeUrl

    companion object {
        /** NativeBridge.raAchievements' texts, 7 per achievement. */
        fun list(parts: Array<ByteArray>): List<RaAchievement> = parts.map { String(it, Charsets.UTF_8) }.chunked(7)
            .filter { it.size == 7 }
            .map { p -> RaAchievement(p[0].toIntOrNull() ?: 0, p[1].toIntOrNull() ?: 0, p[2] == "1", p[3], p[4], p[5], p[6]) }

        /** The game's own achievements, and the server's warning about this app if it sent one (its description). */
        fun split(list: List<RaAchievement>): Pair<List<RaAchievement>, String?> {
            val (warnings, rows) = list.partition { it.id >= RaPopups.WARNING_ID }
            return rows to warnings.firstOrNull()?.description
        }
    }
}

/**
 * What the game screen shows for RetroAchievements (14), against a clock in ms: one card at a time (an unlock,
 * "mastered", a problem, a note) for a few seconds each; the latest progress for 3 s; the challenges going on.
 */
class RaPopups {
    data class Card(val kind: Int, val title: String, val text: String, val points: Int, val badgeUrl: String, val rank: Int = 0,
                    val id: Int = 0) // id: a leaderboard card's board
    data class Challenge(val id: Int, val badgeUrl: String)
    /** A leaderboard's live counter (15): rcheevos' text, e.g. a time "1:23.45" or a score. */
    data class Tracker(val id: Int, val text: String)
    data class View(val card: Card? = null, val progress: Card? = null, val challenges: List<Challenge> = emptyList(),
                    val trackers: List<Tracker> = emptyList())

    /** Settings → "Leaderboard notes on screen" (15): the started / failed notes and the counters; scores always show. */
    var leaderboardNotes = true

    private val waiting = ArrayDeque<Card>()
    private var card: Card? = null
    private var cardUntil = 0L
    private var progress: Card? = null
    private var progressUntil = 0L
    private val challenges = mutableListOf<Challenge>()
    private val trackers = mutableListOf<Tracker>() // every counter going on, shown or not (the switch may change)

    fun add(e: RaEvent, now: Long) {
        when (e.kind) {
            RaEvent.UNLOCKED -> waiting += if (e.id >= WARNING_ID) Card(WARNING, e.title, e.description, 0, "")
                else Card(e.kind, e.title, e.description, e.points, e.badgeUrl)
            RaEvent.MASTERED -> waiting += Card(e.kind, e.title, "Every achievement unlocked", e.points, e.badgeUrl)
            RaEvent.SERVER_ERROR -> waiting += Card(e.kind, "Couldn't send an unlock", e.description, 0, "")
            RaEvent.OFFLINE -> waiting += Card(e.kind, "Offline", "Unlocks are sent when you're back online", 0, "")
            RaEvent.PROGRESS -> {
                progress = Card(e.kind, e.title, e.progress, e.points, e.badgeUrl)
                progressUntil = now + PROGRESS_MS
            }
            RaEvent.PROGRESS_HIDE -> progress = null
            RaEvent.CHALLENGE_SHOW -> if (challenges.none { it.id == e.id }) challenges += Challenge(e.id, e.badgeUrl)
            RaEvent.CHALLENGE_HIDE -> challenges.removeAll { it.id == e.id }
            RaEvent.LB_STARTED -> if (leaderboardNotes) waiting += Card(e.kind, e.title, "Leaderboard started", 0, "", id = e.id)
            RaEvent.LB_FAILED -> if (leaderboardNotes) {
                replaceOrQueue(Card(e.kind, e.title, "Leaderboard failed", 0, "", id = e.id), RaEvent.LB_STARTED, now)
            }
            RaEvent.LB_SUBMITTED -> waiting += Card(e.kind, e.title, "Score ${e.progress} · sending", 0, "", id = e.id)
            RaEvent.LB_SCOREBOARD ->
                replaceOrQueue(Card(e.kind, e.title, scoreLine(e), 0, "", rank = e.rank, id = e.id), RaEvent.LB_SUBMITTED, now)
            RaEvent.LB_ERROR -> waiting += Card(e.kind, "Couldn't send a score",
                listOf(e.title, e.description).filter { it.isNotEmpty() }.joinToString(": "), 0, "")
            RaEvent.TRACKER_SHOW, RaEvent.TRACKER_UPDATE -> {
                val t = Tracker(e.id, e.progress)
                val i = trackers.indexOfFirst { it.id == e.id }
                if (i >= 0) trackers[i] = t else trackers += t
            }
            RaEvent.TRACKER_HIDE -> trackers.removeAll { it.id == e.id }
        }
        tick(now)
    }

    /**
     * A board's newer news takes the place of its older card ([older] of the same board): the one showing (the new one
     * shows at once, for its own time) or the one waiting; else it waits like any card. So a rank never waits behind its
     * own "sending" card, and the queue doesn't run late.
     */
    private fun replaceOrQueue(c: Card, older: Int, now: Long) {
        val shown = card
        if (shown != null && shown.kind == older && shown.id == c.id) {
            card = c
            cardUntil = now + timeOf(c)
            return
        }
        val i = waiting.indexOfFirst { it.kind == older && it.id == c.id }
        if (i >= 0) waiting[i] = c else waiting += c
    }

    private fun timeOf(c: Card) = when (c.kind) {
        RaEvent.UNLOCKED, RaEvent.LB_SCOREBOARD -> UNLOCK_MS
        RaEvent.MASTERED, WARNING -> MASTERED_MS
        RaEvent.LB_STARTED, RaEvent.LB_FAILED -> BOARD_NOTE_MS
        else -> NOTE_MS
    }

    /** A short line of the app's own ("1 of 5 achievements", "RetroAchievements: offline", …). */
    fun note(text: String, now: Long) {
        waiting += Card(NOTE, text, "", 0, "")
        tick(now)
    }

    /** Moves on with time; true when what is shown changed. */
    fun tick(now: Long): Boolean {
        var changed = false
        if (card != null && now >= cardUntil) {
            card = null
            changed = true
        }
        if (card == null && waiting.isNotEmpty()) {
            val c = waiting.removeFirst()
            card = c
            cardUntil = now + timeOf(c)
            changed = true
        }
        if (progress != null && now >= progressUntil) {
            progress = null
            changed = true
        }
        return changed
    }

    fun view() = View(card, progress, challenges.toList(), if (leaderboardNotes) trackers.take(MAX_TRACKERS) else emptyList())

    fun clear() {
        waiting.clear()
        card = null
        progress = null
        challenges.clear()
        trackers.clear()
    }

    companion object {
        const val NOTE = 100
        const val WARNING = 101 // the server's notice about this app (shown as a notice, not an unlock)
        /** RetroAchievements' made-up "achievements" that carry a warning start here (rcheevos' RC_CLIENT_ACHIEVEMENT_WARNING_ID). */
        const val WARNING_ID = 101000001
        const val UNLOCK_MS = 4000L
        const val MASTERED_MS = 6000L
        const val NOTE_MS = 3000L
        const val PROGRESS_MS = 3000L
        const val BOARD_NOTE_MS = 2000L // "Leaderboard started / failed": the least of the cards
        const val MAX_TRACKERS = 3

        /**
         * "Score 0:10.00 · 1,204 players · best 0:09.50" (the best only when it isn't this score): the card's line under
         * the board's name; the rank is stamped beside it (Card.rank), as points are on an unlock.
         */
        fun scoreLine(e: RaEvent): String {
            val players = if (e.entries == 1) "1 player" else String.format(java.util.Locale.US, "%,d players", e.entries)
            return "Score ${e.progress} · $players" + if (e.best.isNotEmpty() && e.best != e.progress) " · best ${e.best}" else ""
        }
    }
}
