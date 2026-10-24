package com.tengen.gbaemu

import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL

/** One download (10b): its bytes, missing (HTTP 404) or failed (anything else). */
object Net {
    sealed interface Result
    class Got(val bytes: ByteArray) : Result
    data object Missing : Result
    data object Failed : Result

    const val USER_AGENT = "GBAEmulator/1.0 (Android)"
    private const val TIMEOUT = 15_000

    /** GET [url] (15 s to connect and to read, redirects followed); more than [max] bytes is a failure. */
    fun get(url: String, max: Int): Result = send(url, null, null, USER_AGENT, max).let {
        when (it.status) {
            200 -> Got(it.body!!)
            404 -> Missing
            else -> Failed
        }
    }

    /** A reply with its HTTP status and body (also for 4xx/5xx), or [NO_REPLY] with no body. */
    class Reply(val status: Int, val body: ByteArray?)

    /** No connection, a timeout, a body over the limit: rcheevos' "retryable client error" (platform/ra/ra.h RA_NO_REPLY). */
    const val NO_REPLY = -2

    /** GET [url], or POST [post] as [contentType]; 15 s to connect and to read; at most [max] bytes back. */
    fun send(url: String, post: ByteArray?, contentType: String?, userAgent: String, max: Int): Reply = try {
        val c = URL(url).openConnection() as HttpURLConnection
        try {
            c.connectTimeout = TIMEOUT
            c.readTimeout = TIMEOUT
            c.instanceFollowRedirects = true
            c.setRequestProperty("User-Agent", userAgent)
            if (post != null) {
                c.requestMethod = "POST"
                c.doOutput = true
                c.setRequestProperty("Content-Type", contentType ?: "application/x-www-form-urlencoded")
                // Not setFixedLengthStreamingMode: in streaming mode Java drops a 401's body, where RetroAchievements
                // puts its "wrong password" message. The forms are small.
                c.outputStream.use { it.write(post) }
            }
            val status = c.responseCode
            val stream = if (status >= 400) c.errorStream else c.inputStream
            if (stream == null) Reply(status, ByteArray(0))
            else stream.use { readUpTo(it, max) }?.let { Reply(status, it) } ?: Reply(NO_REPLY, null)
        } finally {
            c.disconnect()
        }
    } catch (e: Exception) {
        Reply(NO_REPLY, null)
    }

    /** All of [input], or null if it is more than [max] bytes (reading stops there). */
    fun readUpTo(input: InputStream, max: Int): ByteArray? {
        val out = ByteArrayOutputStream()
        val buf = ByteArray(8192)
        while (true) {
            val n = input.read(buf)
            if (n < 0) return out.toByteArray()
            out.write(buf, 0, n)
            if (out.size() > max) return null
        }
    }
}
