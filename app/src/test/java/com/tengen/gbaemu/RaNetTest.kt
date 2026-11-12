package com.tengen.gbaemu

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RaNetTest {
    @Test
    fun aRequestIsReadFromTheNativeParts() {
        val r = RaRequest.of(arrayOf("7", "https://retroachievements.org/dorequest.php", "r=login2", "application/x-www-form-urlencoded"))!!
        assertEquals(7, r.id)
        assertEquals("r=login2", r.post)
        val get = RaRequest.of(arrayOf("8", "https://media.retroachievements.org/Badge/1.png", null, null))!!
        assertNull(get.post)
        assertNull(RaRequest.of(null))
        assertNull(RaRequest.of(arrayOf("x", "u", null, null)))
        assertNull(RaRequest.of(arrayOf("1", null, null, null)))
    }

    @Test
    fun theUserAgentNamesTheAppAndrcheevos() {
        assertEquals("DreamBoy/1.0.0 (Android 16) rcheevos/12.5", RaNet.userAgent("1.0.0", "16", "rcheevos/12.5"))
    }

    @Test
    fun theWorkerAnswersEveryRequestWithItsReply() {
        val queue = java.util.concurrent.LinkedBlockingQueue(listOf(RaRequest(1, "u1", "a", "t"), RaRequest(2, "u2", null, null)))
        val replies = java.util.concurrent.ConcurrentHashMap<Int, Pair<Int, ByteArray?>>()
        val done = CountDownLatch(2)
        val worker = RaWorker(
            take = { ms -> queue.poll(ms.toLong(), TimeUnit.MILLISECONDS) },
            reply = { id, status, body -> replies[id] = status to body; done.countDown() },
        )
        worker.send = { r -> if (r.post != null) Net.Reply(200, "ok ${r.id}".toByteArray()) else Net.Reply(Net.NO_REPLY, null) }
        worker.start()
        assertTrue(done.await(5, TimeUnit.SECONDS))
        worker.stop()
        assertEquals(200, replies[1]!!.first)
        assertArrayEquals("ok 1".toByteArray(), replies[1]!!.second)
        assertEquals(Net.NO_REPLY, replies[2]!!.first)
        assertNull(replies[2]!!.second)
    }
}
