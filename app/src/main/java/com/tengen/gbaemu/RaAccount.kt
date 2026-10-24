package com.tengen.gbaemu

import java.io.File

/**
 * The RetroAchievements sign-in kept on the phone (14): the user's name and login token — never the password — in
 * `files/ra.json`. Backups leave it out (Backup's allow-list) and a restore never touches it.
 */
class RaAccount(private val file: File) {
    fun load(): Pair<String, String>? = try {
        val r = Json.Reader(file.readText())
        val m = r.value() as Map<*, *>
        r.end()
        val user = m["user"] as? String
        val token = m["token"] as? String
        if (user.isNullOrEmpty() || token.isNullOrEmpty()) null else user to token
    } catch (e: Exception) {
        null
    }

    fun save(user: String, token: String): Boolean =
        file.writeSafely("{\"user\": ${Json.str(user)}, \"token\": ${Json.str(token)}}".toByteArray())

    fun clear() {
        file.delete()
    }
}
