package com.tengen.gbaemu

import java.net.InetAddress
import java.net.ServerSocket
import kotlin.concurrent.thread
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NetTest {
    /** A tiny HTTP server on this machine: each path's status and body; the last request's header lines. */
    private class Server(val replies: Map<String, Pair<Int, ByteArray>>) : AutoCloseable {
        val socket = ServerSocket(0, 50, InetAddress.getLoopbackAddress())
        @Volatile var lastRequest = ""
        @Volatile var lastBody = ""
        val url = "http://127.0.0.1:${socket.localPort}"

        init {
            thread(isDaemon = true) {
                while (!socket.isClosed) {
                    val s = try { socket.accept() } catch (e: Exception) { break }
                    s.use {
                        val input = s.getInputStream()
                        val head = generateSequence { readLine(input) }.takeWhile { it.isNotEmpty() }.toList()
                        lastRequest = head.joinToString("\n")
                        val length = head.firstOrNull { it.startsWith("Content-Length:", ignoreCase = true) }
                            ?.substringAfter(':')?.trim()?.toIntOrNull() ?: 0
                        lastBody = String(input.readNBytes(length))
                        val path = head.firstOrNull()?.split(" ")?.getOrNull(1) ?: "/"
                        val (status, body) = replies[path] ?: (404 to ByteArray(0))
                        val out = s.getOutputStream()
                        out.write("HTTP/1.1 $status X\r\nContent-Length: ${body.size}\r\nConnection: close\r\n\r\n".toByteArray())
                        out.write(body)
                        out.flush()
                    }
                }
            }
        }

        override fun close() = socket.close()

        /** One header line (the body is read as bytes after the headers, so no buffered reader may run ahead). */
        private fun readLine(input: java.io.InputStream): String {
            val b = java.io.ByteArrayOutputStream()
            while (true) {
                val c = input.read()
                if (c < 0 || c == '\n'.code) break
                if (c != '\r'.code) b.write(c)
            }
            return b.toString()
        }
    }

    @Test
    fun aReplyIsReadWholeAndA404IsMissing() {
        Server(mapOf("/ok" to (200 to "hello".toByteArray()))).use { s ->
            val got = Net.get("${s.url}/ok", 100)
            assertTrue(got is Net.Got)
            assertEquals("hello", String((got as Net.Got).bytes))
            assertTrue(s.lastRequest, "User-Agent: ${Net.USER_AGENT}" in s.lastRequest)
            assertEquals(Net.Missing, Net.get("${s.url}/gone", 100))
        }
    }

    @Test
    fun tooBigOrAServerErrorIsAFailure() {
        Server(mapOf("/big" to (200 to ByteArray(100)), "/err" to (500 to ByteArray(0)))).use { s ->
            assertEquals(Net.Failed, Net.get("${s.url}/big", 99))
            assertTrue(Net.get("${s.url}/big", 100) is Net.Got) // exactly the limit is fine
            assertEquals(Net.Failed, Net.get("${s.url}/err", 100))
        }
    }

    @Test
    fun nothingListeningOrNoAddressIsAFailure() {
        val port = ServerSocket(0).use { it.localPort } // free again once closed
        assertEquals(Net.Failed, Net.get("http://127.0.0.1:$port/x", 100))
        assertEquals(Net.Failed, Net.get("not a url", 100))
    }

    @Test
    fun sendPostsTheFormAndGivesStatusAndBody() {
        Server(mapOf("/dorequest.php" to (401 to "{\"Success\":false}".toByteArray()))).use { s ->
            val r = Net.send("${s.url}/dorequest.php", "r=login2&u=Tester".toByteArray(), "application/x-www-form-urlencoded",
                "GBAEmulator/1.0 (Android 16) rcheevos/12.5", 1000)
            assertEquals(401, r.status)
            assertEquals("{\"Success\":false}", String(r.body!!))
            assertEquals("r=login2&u=Tester", s.lastBody)
            assertTrue(s.lastRequest, "POST /dorequest.php" in s.lastRequest)
            assertTrue(s.lastRequest, "User-Agent: GBAEmulator/1.0 (Android 16) rcheevos/12.5" in s.lastRequest)
            assertTrue(s.lastRequest, "Content-Type: application/x-www-form-urlencoded" in s.lastRequest)
        }
    }

    @Test
    fun sendWithoutABodyIsAGetAndNoServerIsNoReply() {
        Server(mapOf("/b.png" to (200 to byteArrayOf(1, 2)))).use { s ->
            val r = Net.send("${s.url}/b.png", null, null, "UA", 1000)
            assertEquals(200, r.status)
            assertArrayEquals(byteArrayOf(1, 2), r.body)
            assertTrue(s.lastRequest, "GET /b.png" in s.lastRequest)
            assertEquals(Net.NO_REPLY, Net.send("${s.url}/b.png", null, null, "UA", 1).status) // too big
        }
        assertEquals(Net.NO_REPLY, Net.send("http://127.0.0.1:1/x", null, null, "UA", 1000).status)
    }
}
