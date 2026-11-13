package com.tengen.gbaemu

import java.io.File

/**
 * The edited touch layouts in `files/layout.json`: `{"upright": [...], "sideways": [...]}` for GBA games and, once
 * edited, `"gb_upright"` / `"gb_sideways"` for Game Boy and Game Boy Color games (their own layouts: another picture
 * shape, no L and R), each item
 * `{"id": "a", "x": 2500, "y": 9000, "scale": 15000}` in ten-thousandths ([Json] reads whole numbers). Items not
 * stored are where the default layout puts them. Written safely (a temporary file, then swapped).
 */
class LayoutStore(filesDir: File) {
    data class Layouts(val upright: Map<LayoutItem, Place> = emptyMap(), val sideways: Map<LayoutItem, Place> = emptyMap(),
                       val gbUpright: Map<LayoutItem, Place> = emptyMap(), val gbSideways: Map<LayoutItem, Place> = emptyMap()) {
        /** The edits for a direction and a console family (Console.family: "gba", or "gb"). */
        fun of(sideways: Boolean, family: String = "gba") = when {
            family == "gb" -> if (sideways) gbSideways else gbUpright
            else -> if (sideways) this.sideways else upright
        }

        fun with(sideways: Boolean, edits: Map<LayoutItem, Place>, family: String = "gba") = when {
            family == "gb" -> if (sideways) copy(gbSideways = edits) else copy(gbUpright = edits)
            else -> if (sideways) copy(sideways = edits) else copy(upright = edits)
        }
    }

    private val file = File(filesDir, "layout.json")

    /** True after a [load] that found the file damaged (it is then kept as `layout.json.bad`). */
    var wasDamaged = false
        private set

    fun load(): Layouts {
        wasDamaged = false
        if (!file.isFile) return Layouts()
        return try {
            fromJson(file.readText())
        } catch (e: Exception) {
            file.renameTo(File(file.path + ".bad"))
            wasDamaged = true
            Layouts()
        }
    }

    fun save(layouts: Layouts): Boolean = file.writeSafely(toJson(layouts).toByteArray())

    companion object {
        private const val ONE = 10000f

        fun toJson(l: Layouts): String {
            fun list(edits: Map<LayoutItem, Place>) = edits.entries.sortedBy { it.key.ordinal }.joinToString(",\n", "[\n", "\n  ]") { (item, p) ->
                "    {\"id\": ${Json.str(item.id)}, \"x\": ${(p.x * ONE).toInt()}, \"y\": ${(p.y * ONE).toInt()}, \"scale\": ${(p.scale * ONE).toInt()}}"
            }.replace("[\n\n  ]", "[]")
            // the Game Boy layouts only once there are any: the file of someone who never edits them stays as it was
            val gb = if (l.gbUpright.isEmpty() && l.gbSideways.isEmpty()) ""
                else ",\n  \"gb_upright\": ${list(l.gbUpright)},\n  \"gb_sideways\": ${list(l.gbSideways)}"
            return "{\n  \"upright\": ${list(l.upright)},\n  \"sideways\": ${list(l.sideways)}$gb\n}\n"
        }

        /** Reads what [toJson] writes; values out of range are brought in, unknown items left out; throws on bad JSON. */
        fun fromJson(json: String): Layouts = Json.Reader(json).run {
            var upright = emptyMap<LayoutItem, Place>()
            var sideways = emptyMap<LayoutItem, Place>()
            var gbUpright = emptyMap<LayoutItem, Place>()
            var gbSideways = emptyMap<LayoutItem, Place>()
            obj { key ->
                val edits = LinkedHashMap<LayoutItem, Place>()
                list {
                    var id = ""
                    var x = 0L
                    var y = 0L
                    var scale = ONE.toLong()
                    obj { field ->
                        when (field) {
                            "id" -> id = string()
                            "x" -> x = number()
                            "y" -> y = number()
                            "scale" -> scale = number()
                            else -> error("unknown key $field")
                        }
                    }
                    LayoutItem.byId(id)?.let {
                        edits[it] = Place((x / ONE).coerceIn(0f, 1f), (y / ONE).coerceIn(0f, 1f),
                            TouchLayout.clampScale(scale / ONE))
                    }
                }
                when (key) {
                    "upright" -> upright = edits
                    "sideways" -> sideways = edits
                    "gb_upright" -> gbUpright = edits
                    "gb_sideways" -> gbSideways = edits
                    else -> error("unknown key $key")
                }
            }
            end()
            Layouts(upright, sideways, gbUpright, gbSideways)
        }
    }
}
