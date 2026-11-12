package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class FakeRaServerTest {
    private fun ask(post: String) = FakeRaServer.reply(RaRequest(1, "https://retroachievements.org/dorequest.php", post, "application/x-www-form-urlencoded"))
    private fun text(r: Net.Reply) = String(r.body!!)

    @Test
    fun offlineAnswersNoReply() {
        val flag = java.io.File.createTempFile("ra-fake-offline", "")
        try {
            FakeRaServer.offlineFlag = flag
            assertEquals(Net.NO_REPLY, ask("r=awardachievement&u=Tester&t=TOKEN&a=1&h=0").status)
            flag.delete()
            assertEquals(200, ask("r=awardachievement&u=Tester&t=TOKEN&a=1&h=0").status)
        } finally {
            FakeRaServer.offlineFlag = null
        }
    }

    @Test
    fun itKnowsTesterAndAnExpiredToken() {
        val ok = ask("r=login2&u=Tester&p=secret")
        assertEquals(200, ok.status)
        assertTrue(text(ok), "\"Token\":\"TOKEN\"" in text(ok))
        assertEquals(200, ask("r=login2&u=Tester&t=TOKEN").status)
        assertEquals(401, ask("r=login2&u=Tester&p=wrong").status)
        assertEquals(403, ask("r=login2&u=Tester&t=EXPIRED").status)
    }

    @Test
    fun everyGameHasTheDemoSetUnlessMadeUnknown() {
        val set = ask("r=achievementsets&u=Tester&t=TOKEN&m=abc")
        assertEquals(200, set.status)
        assertTrue("\"ConsoleId\":5" in text(set))
        assertEquals(7, Regex("\"ID\":").findAll(text(set)).count()) // 5 achievements + 2 boards
        assertTrue("\"Title\":\"Sprint\"" in text(set))
        FakeRaServer.unknownHashes = setOf("abc")
        try {
            assertEquals(404, ask("r=achievementsets&u=Tester&t=TOKEN&m=abc").status)
        } finally {
            FakeRaServer.unknownHashes = emptySet()
        }
    }

    @Test
    fun theRefusedAchievementIsRefusedAndTheRestCountDownToMastered() {
        ask("r=startsession&u=Tester&t=TOKEN&g=1234")
        val first = ask("r=awardachievement&u=Tester&t=TOKEN&a=1&h=1")
        assertEquals(200, first.status)
        assertTrue(text(first), "\"AchievementsRemaining\":3" in text(first)) // 2, 3 and 5 left (4 is refused)
        val no = ask("r=awardachievement&u=Tester&t=TOKEN&a=${FakeRaServer.REFUSED_ID}&h=1")
        assertEquals(400, no.status)
        assertTrue("refused" in text(no))
        ask("r=awardachievement&u=Tester&t=TOKEN&a=2&h=1")
        ask("r=awardachievement&u=Tester&t=TOKEN&a=3&h=1")
        assertTrue("\"AchievementsRemaining\":0" in text(ask("r=awardachievement&u=Tester&t=TOKEN&a=5&h=1")))
        assertEquals(200, ask("r=ping&u=Tester&t=TOKEN").status)
    }

    @Test
    fun sprintHasThirtyPlayersWithTesterSeventeenthAndSurvivalNone() {
        val top = text(ask("r=lbinfo&i=${FakeRaServer.SPRINT_ID}&c=10"))
        assertTrue(top, "\"TotalEntries\":30" in top)
        assertEquals(10, Regex("\"Rank\":").findAll(top).count())
        assertTrue("\"Rank\":1," in top && "\"Rank\":10," in top)
        val around = text(ask("r=lbinfo&i=${FakeRaServer.SPRINT_ID}&u=Tester&c=5"))
        assertEquals((15..19).toList(), Regex("\"Rank\":(\\d+)").findAll(around).map { it.groupValues[1].toInt() }.toList())
        assertTrue("\"User\":\"Tester\",\"Rank\":17" in around)
        assertTrue("\"TotalEntries\":0" in text(ask("r=lbinfo&i=${FakeRaServer.SURVIVAL_ID}&c=10")))
    }

    @Test
    fun aScoreIsRankedFourthOfThirtyOne() {
        val r = text(ask("r=submitlbentry&u=Tester&t=TOKEN&i=${FakeRaServer.SPRINT_ID}&s=600&m=abc&v=x"))
        assertTrue(r, "\"Score\":600" in r && "\"Rank\":4" in r && "\"NumEntries\":\"31\"" in r)
    }
}
