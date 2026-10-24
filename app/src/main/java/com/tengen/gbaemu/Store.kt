package com.tengen.gbaemu

import android.content.SharedPreferences

/** Whole numbers by name: the settings' storage (SharedPreferences in the app, a map in the PC-side tests). */
interface IntStore {
    fun get(key: String): Int?

    /** null removes the value. */
    fun put(key: String, value: Int?)

    /** Every value by name (11: backups). */
    fun all(): Map<String, Int>

    /** Removes every value (11: a restore). */
    fun clear()
}

class MemoryStore : IntStore {
    private val map = HashMap<String, Int>()

    override fun get(key: String): Int? = map[key]

    override fun put(key: String, value: Int?) {
        if (value == null) map.remove(key) else map[key] = value
    }

    override fun all(): Map<String, Int> = HashMap(map)

    override fun clear() = map.clear()
}

/** A value of another kind under the same name reads as nothing. */
class PrefsStore(private val prefs: SharedPreferences) : IntStore {
    override fun get(key: String): Int? = if (!prefs.contains(key)) null else try {
        prefs.getInt(key, 0)
    } catch (e: ClassCastException) {
        null
    }

    override fun put(key: String, value: Int?) {
        val edit = prefs.edit()
        if (value == null) edit.remove(key) else edit.putInt(key, value)
        edit.apply()
    }

    override fun all(): Map<String, Int> = prefs.all.mapNotNull { (k, v) -> (v as? Int)?.let { k to it } }.toMap()

    override fun clear() {
        prefs.edit().clear().commit()
    }
}
