package com.tengen.gbaemu

import java.net.URLDecoder
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import kotlin.concurrent.thread

/** One of rcheevos' web requests (14): from NativeBridge.raWaitRequest; [post] null = a GET. */
class RaRequest(val id: Int, val url: String, val post: String?, val contentType: String?) {
    companion object {
        /** From raWaitRequest's [id, url, post, content type]; null if it isn't one. */
        fun of(parts: Array<String?>?): RaRequest? {
            if (parts == null || parts.size != 4) return null
            val id = parts[0]?.toIntOrNull() ?: return null
            val url = parts[1] ?: return null
            return RaRequest(id, url, parts[2], parts[3])
        }
    }
}

object RaNet {
    const val MAX_REPLY = 4 shl 20

    /** The app as RetroAchievements sees it: it recognises approved emulators by this name and version. */
    fun userAgent(version: String, androidRelease: String, rcheevosClause: String) =
        "DreamBoy/$version (Android $androidRelease) $rcheevosClause"

    /** A request's form (`a=1&b=x%20y`) as decoded values. */
    fun params(form: String): Map<String, String> = form.split('&').mapNotNull { pair ->
        val kv = pair.split('=', limit = 2)
        if (kv.size == 2) kv[0] to URLDecoder.decode(kv[1], "UTF-8") else null
    }.toMap()

    fun send(r: RaRequest, userAgent: String): Net.Reply =
        Net.send(r.url, r.post?.toByteArray(Charsets.UTF_8), r.contentType, userAgent, MAX_REPLY)
}

/**
 * Does rcheevos' internet: one thread waits for each request ([take], a timeout in ms) and two more send them ([send])
 * and hand back the answers ([reply]: id, status, body). [send] is the internet, or the pretend server.
 */
class RaWorker(private val take: (Int) -> RaRequest?, private val reply: (Int, Int, ByteArray?) -> Unit) {
    @Volatile var send: (RaRequest) -> Net.Reply = { Net.Reply(Net.NO_REPLY, null) }
    /** Keeps unlocks and scores that got no answer, and sends them once answers come again (16). */
    @Volatile var outbox: RaOutbox? = null
    @Volatile private var running = false
    private var pool: ExecutorService? = null

    @Synchronized
    fun start() {
        if (running) return
        running = true
        val p = Executors.newFixedThreadPool(2) { r -> Thread(r, "ra-send").apply { isDaemon = true } }
        pool = p
        thread(isDaemon = true, name = "ra-requests") {
            while (running) {
                val r = take(1000) ?: continue
                p.execute {
                    val answer = try { send(r) } catch (e: Exception) { Net.Reply(Net.NO_REPLY, null) }
                    reply(r.id, answer.status, answer.body)
                    outbox?.let { box ->
                        box.sent(r, answer.status)
                        if (answer.status != Net.NO_REPLY && box.size > 0) box.resend(send) // connected again
                    }
                }
            }
        }
    }

    @Synchronized
    fun stop() {
        running = false
        pool?.shutdown()
    }
}

/** The app's one worker, for its whole life (the native side's ra_t lives as long). */
object RaService {
    val worker = RaWorker(take = { ms -> RaRequest.of(NativeBridge.raWaitRequest(ms)) }, reply = NativeBridge::raReply)
}
