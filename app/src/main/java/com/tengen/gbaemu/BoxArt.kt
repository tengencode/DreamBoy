package com.tengen.gbaemu

import java.io.File

/**
 * The game list's box art (10b), in `files/boxart/`: what is known per game (by its id) in `boxart.json` — found
 * (its box saved), none (looked up, not found), own (a picture the user chose), removed — and the pictures as
 * `<id>.jpg`. Used from the box-art thread and the UI thread, so every method is synchronized.
 */
class BoxArt(val dir: File) {
    enum class State { FOUND, NONE, OWN, REMOVED }

    /** A game's state, the name it was found under ("" if none) and when it was looked up. */
    data class Entry(val state: State, val name: String = "", val checked: Long = 0)

    private val file = File(dir, "boxart.json")
    private val entries = load()

    @Synchronized
    fun entry(id: String): Entry? = entries[id]

    /** The game's box, if it has one (found or chosen) and its file is there. */
    @Synchronized
    fun picture(id: String): File? {
        val s = entries[id]?.state
        return pictureFile(id).takeIf { (s == State.FOUND || s == State.OWN) && it.isFile }
    }

    /** No entry; none, looked up before the game list of [listTime]; found, but its picture is gone. Never own / removed. */
    @Synchronized
    fun needsLookup(id: String, listTime: Long): Boolean {
        val e = entries[id] ?: return true
        return when (e.state) {
            State.NONE -> e.checked < listTime
            State.FOUND -> !pictureFile(id).isFile
            State.OWN, State.REMOVED -> false
        }
    }

    @Synchronized
    fun found(id: String, name: String, jpeg: ByteArray, now: Long) {
        if (writePicture(id, jpeg)) put(id, Entry(State.FOUND, name, now))
    }

    @Synchronized
    fun none(id: String, name: String, now: Long) = put(id, Entry(State.NONE, name, now))

    /** The user's own picture (false if it couldn't be written); the name the game was found under is kept. */
    @Synchronized
    fun own(id: String, jpeg: ByteArray): Boolean {
        if (!writePicture(id, jpeg)) return false
        put(id, Entry(State.OWN, entries[id]?.name ?: "", entries[id]?.checked ?: 0))
        return true
    }

    @Synchronized
    fun remove(id: String) {
        pictureFile(id).delete()
        put(id, Entry(State.REMOVED, entries[id]?.name ?: "", entries[id]?.checked ?: 0))
    }

    /** The user's own choices (own pictures and removed boxes), for a backup (11). */
    @Synchronized
    fun choices(): Map<String, State> =
        entries.filterValues { it.state == State.OWN || it.state == State.REMOVED }.mapValues { it.value.state }

    /**
     * A restore (11): the user's own choices become [choices] (an own one needs its picture in [pictures], else it is
     * left out); the old own pictures are deleted; found and not-found entries stay.
     */
    @Synchronized
    fun replaceChoices(choices: Map<String, State>, pictures: Map<String, ByteArray>) {
        for ((id, e) in entries.toList()) if (e.state == State.OWN || e.state == State.REMOVED) {
            if (e.state == State.OWN) pictureFile(id).delete()
            entries.remove(id)
        }
        for ((id, state) in choices) {
            val old = entries[id]
            when (state) {
                State.OWN -> {
                    val jpeg = pictures[id] ?: continue
                    if (writePicture(id, jpeg)) entries[id] = Entry(State.OWN, old?.name ?: "", old?.checked ?: 0)
                }
                State.REMOVED -> {
                    pictureFile(id).delete()
                    entries[id] = Entry(State.REMOVED, old?.name ?: "", old?.checked ?: 0)
                }
                else -> {}
            }
        }
        write()
    }

    private fun pictureFile(id: String) = File(dir, "$id.jpg")

    private fun writePicture(id: String, jpeg: ByteArray): Boolean = pictureFile(id).writeSafely(jpeg)

    private fun put(id: String, e: Entry) {
        entries[id] = e
        write()
    }

    private fun write() {
        file.writeSafely(toJson(entries).toByteArray()) // if not, kept in memory; written with the next change
    }

    /** The entries; empty without a file, or if it is damaged (it is then kept as `.json.bad`). */
    private fun load(): MutableMap<String, Entry> {
        if (!file.isFile) return mutableMapOf()
        return try {
            fromJson(file.readText())
        } catch (x: Exception) {
            file.renameTo(File(file.path + ".bad"))
            mutableMapOf()
        }
    }

    companion object {
        fun toJson(entries: Map<String, Entry>): String = entries.entries.joinToString(",\n", "{\n", "\n}\n") { (id, e) ->
            "  ${Json.str(id)}: {\"state\": ${Json.str(e.state.name.lowercase())}, \"name\": ${Json.str(e.name)}, " +
                "\"checked\": ${e.checked}}"
        }

        fun fromJson(json: String): MutableMap<String, Entry> = Json.Reader(json).run {
            val out = mutableMapOf<String, Entry>()
            obj { id ->
                var state: State? = null
                var name = ""
                var checked = 0L
                obj { key ->
                    when (key) {
                        "state" -> state = State.valueOf(string().uppercase())
                        "name" -> name = string()
                        "checked" -> checked = number()
                        else -> error("unknown key $key")
                    }
                }
                out[id] = Entry(state ?: error("no state for $id"), name, checked)
            }
            end()
            out
        }
    }
}
