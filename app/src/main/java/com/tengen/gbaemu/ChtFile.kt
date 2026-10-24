package com.tengen.gbaemu

/**
 * libretro's `.cht` cheat files (milestone 10a): `cheatN_desc = "name"` and `cheatN_code = "A+B+C"` lines, read into
 * names and codes as the cheat pop-up takes them (one code per line). Pure: tested on the PC.
 */
object ChtFile {
    /** What an import did: the game's new list, and how many were added, couldn't be read, were already there. */
    data class Merge(val cheats: List<Cheat>, val added: Int, val unreadable: Int, val already: Int)

    /** The note when the file has no cheat at all (none with a code, not text, too big, can't be opened). */
    const val NO_CHEATS = "This file has no cheats."

    /** The biggest file read: libretro's GBA files are far smaller. */
    const val MAX_BYTES = 1 shl 20

    private val KEY = Regex("""cheat(\d+)_(desc|code)""")
    private val HEX = Regex("[0-9A-Fa-f]+")

    /** (name, codes) per cheat that has a code, in N order; empty for anything that isn't a `.cht`. */
    fun parse(text: String): List<Pair<String, String>> {
        val names = HashMap<Int, String>()
        val codes = HashMap<Int, String>()
        for (raw in text.removePrefix("﻿").lines()) {
            val line = raw.trim()
            if (line.isEmpty() || line.startsWith("#")) continue
            val eq = line.indexOf('=')
            if (eq < 0) continue
            val key = KEY.matchEntire(line.substring(0, eq).trim()) ?: continue
            val n = key.groupValues[1].toIntOrNull() ?: continue
            val value = unquote(line.substring(eq + 1).trim())
            if (key.groupValues[2] == "desc") names[n] = value else codes[n] = value
        }
        return codes.keys.sorted().mapNotNull { n ->
            val lines = codeLines(codes.getValue(n))
            if (lines.isEmpty()) null else (names[n]?.takeIf { it.isNotBlank() } ?: "Cheat ${n + 1}") to lines
        }
    }

    /**
     * A file's code (parts joined with `+`) as lines: an 8-hex-digit part and a 4- or 8-hex-digit part after it make
     * one line `AAAAAAAA BBBB`; any other part is a line on its own. Spaces inside a part are removed.
     */
    fun codeLines(code: String): String {
        val parts = code.split('+').map { p -> p.filterNot { it == ' ' || it == '\t' } }.filter { it.isNotEmpty() }
        val lines = mutableListOf<String>()
        var i = 0
        while (i < parts.size) {
            val a = parts[i]
            val b = parts.getOrNull(i + 1)
            if (a.length == 8 && HEX.matches(a) && b != null && (b.length == 4 || b.length == 8) && HEX.matches(b)) {
                lines += "$a $b"
                i += 2
            } else {
                lines += a
                i++
            }
        }
        return lines.joinToString("\n")
    }

    /**
     * The game's cheats, then the file's new ones (switched off, type Auto). One with the same name and codes as a
     * cheat already in the list (or earlier in the file) is already there; one [readable] refuses is skipped.
     */
    fun merge(existing: List<Cheat>, found: List<Pair<String, String>>, readable: (String) -> Boolean): Merge {
        val seen = existing.mapTo(HashSet()) { same(it.name, it.codes) }
        val out = existing.toMutableList()
        var unreadable = 0
        var already = 0
        for ((name, codes) in found) {
            when {
                !seen.add(same(name, codes)) -> already++
                !readable(codes) -> unreadable++
                else -> out += Cheat(name, NativeBridge.CHEAT_AUTO, codes, on = false)
            }
        }
        return Merge(out, out.size - existing.size, unreadable, already)
    }

    /** The note shown after an import that found cheats. */
    fun note(m: Merge): String {
        val first = when {
            m.added == 1 -> "Added 1 cheat."
            m.added > 1 -> "Added ${m.added} cheats."
            m.already > 0 -> "No new cheats in this file."
            else -> "No cheats could be read from this file."
        }
        return if (m.unreadable > 0 && (m.added > 0 || m.already > 0)) "$first ${m.unreadable} couldn't be read." else first
    }

    /** Codes compare without spaces or line breaks, in any case. */
    private fun same(name: String, codes: String) = name to codes.filterNot(Char::isWhitespace).uppercase()

    /** A value in quotes (where `\"` and `\\` stand for `"` and `\`), or a bare one as it is. */
    private fun unquote(v: String): String {
        if (!v.startsWith('"')) return v
        val out = StringBuilder()
        var i = 1
        while (i < v.length) {
            val c = v[i]
            if (c == '"') break
            if (c == '\\' && i + 1 < v.length && (v[i + 1] == '"' || v[i + 1] == '\\')) {
                out.append(v[i + 1])
                i += 2
                continue
            }
            out.append(c)
            i++
        }
        return out.toString()
    }
}
