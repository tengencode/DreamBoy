package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class RaOutboxTest {
    private val file = File(Files.createTempDirectory("outbox").toFile(), "ra-outbox.json")
    private var clock = 1_000_000L
    private fun box() = RaOutbox(file) { clock }

    private val url = "https://retroachievements.org/dorequest.php"
    private fun req(post: String) = RaRequest(7, url, post, "application/x-www-form-urlencoded")
    private fun award(id: Int = 1001, user: String = "Tester", o: Long = 0) =
        req("r=awardachievement&u=$user&t=TOKEN&a=$id&h=0&m=abc" + (if (o > 0) "&o=$o" else "") + "&v=x")
    private fun score(s: Int) = req("r=submitlbentry&u=Tester&t=TOKEN&i=2001&s=$s&m=abc&v=x")

    /** Sends through [answer], remembering what was sent. */
    private class Sender(val answer: Int) {
        val sent = mutableListOf<RaRequest>()
        fun send(r: RaRequest): Net.Reply { sent += r; return Net.Reply(answer, if (answer == Net.NO_REPLY) null else "{}".toByteArray()) }
    }

    @Test
    fun signaturesMatchRcheevos() {
        // The same values as tests/unit/test_ra.c's ra_award_and_score_signatures_for_the_outbox, made by rcheevos.
        assertEquals("e47388663084231e1df43ac2611c6687", RaOutbox.awardSignature(1001, "Tester", 0, 90))
        assertEquals("ba409acb0b83555ccca0863531b10f6d", RaOutbox.awardSignature(1001, "Tester", 0, 0))
        assertEquals("725eb09a0efa54931ee41fa8fa759c5e", RaOutbox.scoreSignature(2001, "Tester", 1234, 60))
    }

    @Test
    fun anUnlockThatCantBeSentIsKeptAcrossARestart() {
        box().sent(award(), Net.NO_REPLY)
        val after = box() // the app was closed and opened again
        assertEquals(1, after.size)
        clock += 120_000
        val s = Sender(200)
        assertEquals(1, after.resend(s::send))
        val p = FakeRaServer.params(s.sent.single().post!!)
        assertEquals("120", p["o"])
        assertEquals(RaOutbox.awardSignature(1001, "Tester", 0, 120), p["v"])
        assertEquals("1001", p["a"])
        assertEquals("TOKEN", p["t"])
        assertEquals(url, s.sent.single().url)
        assertEquals(0, after.size)
        assertEquals(0, box().size)
    }

    @Test
    fun anAnswerRemovesIt() {
        val b = box()
        b.sent(award(), Net.NO_REPLY)
        b.sent(award(o = 3), 200) // rcheevos' own retry got through
        assertEquals(0, b.size)
        b.sent(award(), Net.NO_REPLY)
        b.sent(award(), 400) // refused: sending again won't help
        assertEquals(0, b.size)
    }

    @Test
    fun stillOfflineKeepsIt() {
        val b = box()
        b.sent(award(), Net.NO_REPLY)
        clock += 30_000
        assertEquals(0, b.resend(Sender(Net.NO_REPLY)::send))
        assertEquals(1, b.size)
        clock += 30_000
        val s = Sender(200)
        b.resend(s::send)
        assertEquals("60", FakeRaServer.params(s.sent.single().post!!)["o"]) // still counted from the first failure
    }

    @Test
    fun anOffsetRcheevosAlreadyAddedIsKept() {
        val b = box()
        b.sent(award(o = 5), Net.NO_REPLY) // rcheevos' retry, 5 s after the unlock
        clock += 10_000
        val s = Sender(200)
        b.resend(s::send)
        val p = FakeRaServer.params(s.sent.single().post!!)
        assertEquals("15", p["o"])
        assertEquals(RaOutbox.awardSignature(1001, "Tester", 0, 15), p["v"])
    }

    @Test
    fun aBusyOrDownServerKeepsIt() {
        // rcheevos retries these too (rc_client_should_retry): maintenance, a full pool, Cloudflare, too many at once.
        for (status in listOf(429, 502, 503, 504, 521, 522, 523, 524, 525)) {
            val one = RaOutbox(File(file.parentFile, "busy-$status.json")) { clock }
            one.sent(award(), status) // rcheevos retries it, but only while the app runs
            assertEquals("kept after $status", 1, one.size)
        }
        val b = box()
        b.sent(award(), Net.NO_REPLY)
        b.sent(award(o = 2), 503) // rcheevos' own retry hit maintenance
        assertEquals(1, b.size)
        assertEquals(0, b.resend(Sender(503)::send))
        assertEquals(1, b.size)
        assertEquals(1, b.resend(Sender(200)::send))
        assertEquals(0, b.size)
    }

    @Test
    fun aResendStopsAtTheFirstUnansweredOne() {
        val b = box()
        b.sent(award(1), Net.NO_REPLY)
        b.sent(award(2), Net.NO_REPLY)
        b.sent(award(3), Net.NO_REPLY)
        val s = Sender(Net.NO_REPLY)
        b.resend(s::send)
        assertEquals(1, s.sent.size) // the connection is gone: don't wait out a timeout for each
        assertEquals(3, b.size)
    }

    @Test
    fun repeatedFailuresAreKeptOnce() {
        val b = box()
        b.sent(award(), Net.NO_REPLY)
        clock += 1_000
        b.sent(award(o = 1), Net.NO_REPLY)
        clock += 2_000
        b.sent(award(o = 3), Net.NO_REPLY)
        assertEquals(1, b.size)
        val s = Sender(200)
        assertEquals(1, b.resend(s::send))
        assertEquals("3", FakeRaServer.params(s.sent.single().post!!)["o"]) // from the first failure, not the last
    }

    @Test
    fun twoScoresOnOneBoardAreBothKept() {
        val b = box()
        b.sent(score(1234), Net.NO_REPLY)
        b.sent(score(1300), Net.NO_REPLY)
        assertEquals(2, b.size)
        clock += 60_000
        val s = Sender(200)
        assertEquals(2, b.resend(s::send))
        val first = FakeRaServer.params(s.sent.first { "s=1234" in it.post!! }.post!!)
        assertEquals(RaOutbox.scoreSignature(2001, "Tester", 1234, 60), first["v"])
    }

    @Test
    fun aUserNameIsSignedDecoded() {
        val b = box()
        b.sent(award(user = "Bob%20Smith%2B1"), Net.NO_REPLY)
        clock += 7_000
        val s = Sender(200)
        b.resend(s::send)
        val post = s.sent.single().post!!
        assertEquals(RaOutbox.awardSignature(1001, "Bob Smith+1", 0, 7), FakeRaServer.params(post)["v"])
        assertTrue(post, "u=Bob%20Smith%2B1" in post) // the rest of the form is sent as rcheevos wrote it
    }

    @Test
    fun aDamagedFileIsEmptyNotACrash() {
        file.writeText("{\"broken")
        val b = box()
        assertEquals(0, b.size)
        b.sent(award(), Net.NO_REPLY)
        assertEquals(1, box().size)
    }

    @Test
    fun otherRequestsAreIgnored() {
        val b = box()
        b.sent(req("r=login2&u=Tester&t=TOKEN"), Net.NO_REPLY)
        b.sent(req("r=lbinfo&i=2001&c=10"), Net.NO_REPLY)
        b.sent(RaRequest(1, "$url?r=gameinfo", null, null), Net.NO_REPLY)
        assertEquals(0, b.size)
    }

    @Test
    fun clearForgetsEverything() {
        val b = box()
        b.sent(award(), Net.NO_REPLY)
        b.clear()
        assertEquals(0, b.size)
        assertEquals(0, box().size)
    }
}
