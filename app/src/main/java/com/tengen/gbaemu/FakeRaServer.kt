package com.tengen.gbaemu

import java.io.File

/**
 * A pretend RetroAchievements server inside the app (14), answering like the real one (and like platform/ra/ra_fake.c):
 * user "Tester", password "secret", token "TOKEN" ("EXPIRED" has expired). Every game has the demo set below — five
 * achievements that unlock by themselves as frames go by (a condition that is always true, with a hit count): an unlock
 * at 5 s, a progress bar to 20 s, a challenge until 30 s, a refused unlock at 10 s and "mastered" at 40 s. And two
 * demo boards (15), run only in hardcore (rcheevos' rule): Sprint starts at 2 s, counts the frames and is sent at 12 s
 * (ranked 4th of 31); Survival starts at 4 s and fails at 15 s. Scores: Sprint has 30 players with Tester 17th,
 * Survival none.
 * Used by the on-device tests and, in a debug build with `files/ra-fake`, by the app (Claude's emulator check).
 */
object FakeRaServer {
    const val REFUSED_ID = 4
    const val SPRINT_ID = 11
    const val SURVIVAL_ID = 12
    private const val USER_RANK = 17
    @Volatile var unknownHashes: Set<String> = emptySet()

    private fun ach(id: Int, title: String, desc: String, points: Int, mem: String) =
        "{\"ID\":$id,\"Title\":\"$title\",\"Description\":\"$desc\",\"Flags\":3,\"Points\":$points,\"MemAddr\":\"$mem\"," +
            "\"Author\":\"Test\",\"BadgeName\":\"0000$id\",\"Created\":1,\"Modified\":1}"

    private val DEMO_SET = listOf(
        ach(1, "Welcome", "Play for 5 seconds", 5, "0xH000000>=0.300."),
        ach(2, "Getting There", "Play for 20 seconds", 10, "M:0xH000000>=0.1200."),
        ach(3, "Daredevil", "Keep going for 30 seconds", 25, "0xH000000>=0_T:0xH000000>=0.1800."),
        ach(REFUSED_ID, "Refused", "The server says no", 1, "0xH000000>=0.600."),
        ach(5, "The Long Wait", "Play for 40 seconds", 50, "0xH000000>=0.2400."),
    ).joinToString(",", "[", "]")

    private fun board(id: Int, title: String, desc: String, format: String, lower: Boolean, mem: String) =
        "{\"ID\":$id,\"Title\":\"$title\",\"Description\":\"$desc\",\"Mem\":\"$mem\",\"Format\":\"$format\"," +
            "\"LowerIsBetter\":$lower,\"Hidden\":false}"

    // Started, counted and submitted by hit counts alone, so they run in any game (checked on the PC by
    // ra_a_hit_count_value_counts_frames).
    private val DEMO_BOARDS = listOf(
        board(SPRINT_ID, "Sprint", "Finish as fast as you can", "FRAMES", true,
            "STA:0xH000000>=0.120.::CAN:0=1::SUB:0xH000000>=0.720.::VAL:M:0xH000000>=0"),
        board(SURVIVAL_ID, "Survival", "Last as long as you can", "VALUE", false,
            "STA:0xH000000>=0.240.::CAN:0xH000000>=0.900.::SUB:0=1::VAL:M:0xH000000>=0"),
    ).joinToString(",", "[", "]")

    private const val LOGIN_OK = "{\"Success\":true,\"User\":\"Tester\",\"Token\":\"TOKEN\",\"Score\":100,\"SoftcoreScore\":5," +
        "\"Messages\":0,\"Permissions\":1,\"AccountType\":\"Registered\"}"
    private const val LOGIN_BAD = "{\"Success\":false,\"Error\":\"Invalid User/Password combination. Please try again.\"," +
        "\"Status\":401,\"Code\":\"invalid_credentials\"}"
    private const val LOGIN_EXPIRED = "{\"Success\":false,\"Error\":\"The access token has expired. Please log in again.\"," +
        "\"Status\":401,\"Code\":\"expired_token\"}"
    private const val NOT_FOUND = "{\"Success\":false,\"Error\":\"Unknown game\",\"Code\":\"not_found\",\"Status\":404}"

    fun params(form: String): Map<String, String> = RaNet.params(form)

    /** While this file exists every request goes unanswered, as with no connection (16: the outbox check). */
    @Volatile var offlineFlag: File? = null

    fun reply(r: RaRequest): Net.Reply {
        if (offlineFlag?.exists() == true) return Net.Reply(Net.NO_REPLY, null)
        val p = params(r.post ?: r.url.substringAfter('?', ""))
        fun say(status: Int, body: String) = Net.Reply(status, body.toByteArray())
        return when (p["r"]) {
            "login2" -> when {
                p["t"] == "EXPIRED" -> say(403, LOGIN_EXPIRED)
                p["u"] == "Tester" && (p["p"] == "secret" || p["t"] == "TOKEN") -> say(200, LOGIN_OK)
                else -> say(401, LOGIN_BAD)
            }
            "achievementsets" -> if (p["m"] in unknownHashes) say(404, NOT_FOUND) else say(200,
                "{\"Success\":true,\"GameId\":1234,\"Title\":\"Demo Set\",\"ConsoleId\":5," +
                    "\"ImageIconUrl\":\"https://media.retroachievements.org/Images/000001.png\",\"RichPresenceGameId\":1234," +
                    "\"RichPresencePatch\":\"\",\"Sets\":[{\"AchievementSetId\":1111,\"GameId\":1234,\"Title\":null," +
                    "\"Type\":\"core\",\"ImageIconUrl\":\"https://media.retroachievements.org/Images/000001.png\"," +
                    "\"Achievements\":$DEMO_SET,\"Leaderboards\":$DEMO_BOARDS}]}")
            "startsession" -> {
                synchronized(awarded) { awarded.clear() }
                say(200, "{\"Success\":true,\"Unlocks\":[],\"HardcoreUnlocks\":[]}")
            }
            "awardachievement" -> if (p["a"] == REFUSED_ID.toString()) {
                say(400, "{\"Success\":false,\"Error\":\"The server refused this unlock.\"}")
            } else {
                // How many are left, as the real server says (rcheevos shows "mastered" at 0); the refused one counts as
                // done here, so the demo still ends with "mastered".
                val left = synchronized(awarded) {
                    p["a"]?.toIntOrNull()?.let { awarded += it }
                    (DEMO_IDS - awarded - REFUSED_ID).size
                }
                say(200, "{\"Success\":true,\"Score\":105,\"SoftcoreScore\":5,\"AchievementID\":${p["a"]},\"AchievementsRemaining\":$left}")
            }
            "lbinfo" -> {
                val id = p["i"]?.toIntOrNull() ?: 0
                val count = p["c"]?.toIntOrNull() ?: 10
                val total = if (id == SPRINT_ID) 30 else 0
                val first = if (p["u"] != null) (USER_RANK - count / 2).coerceIn(1, maxOf(1, total - count + 1))
                    else (p["o"]?.toIntOrNull() ?: 0) + 1
                val rows = (first until first + count).filter { it <= total }.joinToString(",") { r ->
                    val name = if (r == USER_RANK) "Tester" else String.format(java.util.Locale.US, "Player%02d", r)
                    "{\"User\":\"$name\",\"Rank\":$r,\"Index\":$r,\"Score\":${540 + 30 * r},\"DateSubmitted\":1767225600}"
                }
                say(200, "{\"Success\":true,\"LeaderboardData\":{\"LBID\":$id,\"LBFormat\":\"FRAMES\",\"LowerIsBetter\":1," +
                    "\"LBTitle\":\"Sprint\",\"LBDesc\":\"-\",\"LBMem\":\"-\",\"GameID\":1234,\"LBAuthor\":\"Test\"," +
                    "\"LBCreated\":\"2026-01-01 00:00:00\",\"LBUpdated\":\"2026-01-01 00:00:00\",\"Entries\":[$rows]," +
                    "\"TotalEntries\":$total}}")
            }
            "submitlbentry" -> {
                val s = p["s"] ?: "0"
                say(200, "{\"Success\":true,\"Response\":{\"Score\":$s,\"BestScore\":$s,\"LBData\":{\"Format\":\"FRAMES\"," +
                    "\"LeaderboardID\":${p["i"]},\"GameID\":1234,\"Title\":\"Sprint\",\"LowerIsBetter\":1}," +
                    "\"RankInfo\":{\"Rank\":4,\"NumEntries\":\"31\",\"LowerIsBetter\":1,\"UserRank\":4}," +
                    "\"TopEntries\":[{\"User\":\"Player01\",\"Rank\":1,\"Score\":570}]}}")
            }
            else -> say(200, "{\"Success\":true}")
        }
    }

    private val DEMO_IDS = setOf(1, 2, 3, REFUSED_ID, 5)
    private val awarded = mutableSetOf<Int>()
}
