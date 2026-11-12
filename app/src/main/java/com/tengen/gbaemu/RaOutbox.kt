package com.tengen.gbaemu

import java.io.File
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Unlocks and leaderboard scores that couldn't reach RetroAchievements (16): kept in [file] (app-private, never in
 * backups) and sent again later — also after the app was closed, which rcheevos' own retries don't survive.
 * RetroAchievements requires this of approved emulators.
 */
class RaOutbox(private val file: File, private val now: () -> Long = System::currentTimeMillis) {
    /** One request rcheevos made; [at] is when the unlock (or score) happened, in ms. */
    private class Entry(val url: String, val post: String, val contentType: String?, val at: Long)

    private val entries = load()
    private val resending = AtomicBoolean(false)

    val size: Int @Synchronized get() = entries.size

    /** After each of rcheevos' requests: an unlock or a score with no answer is kept; any answer forgets it. */
    @Synchronized
    fun sent(r: RaRequest, status: Int) {
        val post = r.post ?: return
        val k = key(post) ?: return
        if (status == Net.NO_REPLY) {
            if (entries.any { key(it.post) == k }) return
            val offset = RaNet.params(post)["o"]?.toLongOrNull() ?: 0
            entries += Entry(r.url, post, r.contentType, now() - offset * 1000)
            save()
        } else if (entries.removeAll { key(it.post) == k }) {
            save()
        }
    }

    /**
     * Sends what is kept, each with its time since the unlock brought up to date; any answer (also a refusal) forgets
     * one. Slow: not on the UI thread. How many the server took.
     */
    fun resend(send: (RaRequest) -> Net.Reply): Int {
        if (!resending.compareAndSet(false, true)) return 0
        try {
            var took = 0
            for (e in synchronized(this) { entries.toList() }) {
                val status = try { send(RaRequest(0, e.url, resigned(e), e.contentType)).status } catch (x: Exception) { Net.NO_REPLY }
                if (status == Net.NO_REPLY) continue
                if (status in 200..299) took++
                synchronized(this) { if (entries.remove(e)) save() }
            }
            return took
        } finally {
            resending.set(false)
        }
    }

    @Synchronized
    fun clear() {
        entries.clear()
        file.delete()
    }

    /** [e]'s form with `o` (seconds since the unlock) and `v` (the signature) made again for now. */
    private fun resigned(e: Entry): String {
        val p = RaNet.params(e.post)
        val o = maxOf(0, (now() - e.at) / 1000)
        val user = p["u"] ?: ""
        val v = if (p["r"] == AWARD) awardSignature(p["a"]!!.toInt(), user, p["h"]?.toIntOrNull() ?: 0, o)
            else scoreSignature(p["i"]!!.toInt(), user, p["s"]!!.toInt(), o)
        val kept = e.post.split('&').filter { it.substringBefore('=') !in setOf("o", "v") }
        return (kept + listOfNotNull(if (o > 0) "o=$o" else null, "v=$v")).joinToString("&")
    }

    private fun save() {
        val json = entries.joinToString(",", "[", "]") {
            "{\"url\":${Json.str(it.url)},\"post\":${Json.str(it.post)}," +
                "\"type\":${it.contentType?.let(Json::str) ?: "null"},\"at\":${it.at}}"
        }
        file.writeSafely(json.toByteArray())
    }

    private fun load(): MutableList<Entry> {
        val out = mutableListOf<Entry>()
        try {
            if (!file.isFile) return out
            val r = Json.Reader(file.readText())
            r.list {
                var url = ""
                var post = ""
                var type: String? = null
                var at = 0L
                r.obj { k ->
                    when (k) {
                        "url" -> url = r.string()
                        "post" -> post = r.string()
                        "type" -> type = if (r.nullValue()) null else r.string()
                        "at" -> at = r.number()
                        else -> r.value()
                    }
                }
                if (key(post) != null) out += Entry(url, post, type, at)
            }
            r.end()
        } catch (e: Exception) { // damaged (e.g. a power cut while writing): start empty
            out.clear()
        }
        return out
    }

    companion object {
        private const val AWARD = "awardachievement"
        private const val SCORE = "submitlbentry"

        /** Which unlock or score a form is (null: neither): the same unlock retried is one; two scores are two. */
        private fun key(post: String): String? {
            val p = RaNet.params(post)
            return when (p["r"]) {
                AWARD -> p["a"]?.toIntOrNull()?.let { "a$it h${p["h"]}" }
                SCORE -> if (p["i"]?.toIntOrNull() != null && p["s"]?.toIntOrNull() != null) "i${p["i"]} s${p["s"]}" else null
                else -> null
            }
        }

        // As rcheevos signs them (rc_api_runtime.c; checked against it in tests/unit/test_ra.c).
        fun awardSignature(id: Int, user: String, hardcore: Int, offset: Long): String =
            md5("$id$user$hardcore" + if (offset > 0) "$id$offset" else "")

        fun scoreSignature(board: Int, user: String, score: Int, offset: Long): String =
            md5("$board$user$score" + if (offset > 0) "$offset" else "")

        private fun md5(s: String): String =
            MessageDigest.getInstance("MD5").digest(s.toByteArray()).joinToString("") { "%02x".format(it) }
    }
}
