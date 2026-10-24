package com.tengen.gbaemu

import java.io.File

/** One cheat: its name, the type picked (NativeBridge.CHEAT_*), its codes as typed, and whether it is switched on. */
data class Cheat(val name: String, val type: Int, val codes: String, val on: Boolean)

/**
 * A game's cheats in `files/cheats/<game id>.json`: a list of {name, type, codes, on}. Written safely (a temporary
 * file, then swapped), with [Json].
 */
class Cheats(filesDir: File, gameId: String) {
    private val file = File(filesDir, "cheats/$gameId.json")

    /** The cheats; empty without a file; null if the file is damaged (it is then kept as `.json.bad`). */
    fun load(): List<Cheat>? {
        if (!file.isFile) return emptyList()
        return try {
            fromJson(file.readText())
        } catch (e: Exception) {
            file.renameTo(File(file.path + ".bad"))
            null
        }
    }

    fun save(cheats: List<Cheat>): Boolean = file.writeSafely(toJson(cheats).toByteArray())

    companion object {
        fun toJson(cheats: List<Cheat>): String = cheats.joinToString(",\n", "[\n", "\n]\n") {
            "  {\"name\": ${Json.str(it.name)}, \"type\": ${it.type}, \"codes\": ${Json.str(it.codes)}, \"on\": ${it.on}}"
        }

        /** Reads what [toJson] writes (and any JSON of that shape); throws on anything else. */
        fun fromJson(json: String): List<Cheat> = Json.Reader(json).run {
            val cheats = mutableListOf<Cheat>()
            list {
                var name = ""
                var type = 0
                var codes = ""
                var on = false
                obj { key ->
                    when (key) {
                        "name" -> name = string()
                        "type" -> type = number().toInt()
                        "codes" -> codes = string()
                        "on" -> on = bool()
                        else -> error("unknown key $key")
                    }
                }
                cheats += Cheat(name, type, codes, on)
            }
            end()
            cheats
        }
    }
}
