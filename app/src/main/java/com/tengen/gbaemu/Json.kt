package com.tengen.gbaemu

/**
 * The small JSON the app writes (cheats, the game list) and reads (also Delta skins, 9): strings, numbers, true/false,
 * lists and objects of those. By hand, so it is tested on the PC without Android's org.json.
 */
object Json {
    /** [s] as a JSON string, quotes included. */
    fun str(s: String): String = buildString {
        append('"')
        for (c in s) when {
            c == '"' -> append("\\\"")
            c == '\\' -> append("\\\\")
            c == '\n' -> append("\\n")
            c == '\r' -> append("\\r")
            c == '\t' -> append("\\t")
            c < ' ' -> append("\\u%04x".format(c.code))
            else -> append(c)
        }
        append('"')
    }

    /** Reads JSON piece by piece; every call throws on anything unexpected. */
    class Reader(private val s: String) {
        private var i = 0

        /** The next character after any spaces, or null at the end. */
        fun peek(): Char? {
            while (i < s.length && s[i].isWhitespace()) i++
            return if (i < s.length) s[i] else null
        }

        fun expect(c: Char) {
            if (peek() != c) error("expected $c at $i")
            i++
        }

        /** The next character, which must be [a] or [b]. */
        fun take(a: Char, b: Char): Char {
            val c = peek()
            if (c != a && c != b) error("expected $a or $b at $i")
            i++
            return c!!
        }

        fun string(): String {
            expect('"')
            val out = StringBuilder()
            while (true) {
                if (i >= s.length) error("unfinished string")
                val c = s[i++]
                when (c) {
                    '"' -> return out.toString()
                    '\\' -> {
                        if (i >= s.length) error("unfinished escape")
                        when (val e = s[i++]) {
                            '"', '\\', '/' -> out.append(e)
                            'n' -> out.append('\n')
                            'r' -> out.append('\r')
                            't' -> out.append('\t')
                            'b' -> out.append('\b')
                            'f' -> out.append('\u000C')
                            'u' -> {
                                if (i + 4 > s.length) error("short \\u")
                                out.append(s.substring(i, i + 4).toInt(16).toChar())
                                i += 4
                            }
                            else -> error("bad escape \\$e")
                        }
                    }
                    else -> out.append(c)
                }
            }
        }

        fun number(): Long {
            peek()
            val start = i
            if (i < s.length && s[i] == '-') i++
            while (i < s.length && s[i].isDigit()) i++
            return s.substring(start, i).toLongOrNull() ?: error("expected a number at $start")
        }

        /** A number with an optional fraction and exponent (`47.5`, `-3`, `1e2`). */
        fun decimal(): Double {
            peek()
            val start = i
            while (i < s.length && (s[i].isDigit() || s[i] in "+-.eE")) i++
            return s.substring(start, i).toDoubleOrNull() ?: error("expected a number at $start")
        }

        /** Any value as plain Kotlin: an object a map (keys in order), a list, a string, a number (Double), true/false, null. */
        fun value(): Any? = when (peek()) {
            '{' -> LinkedHashMap<String, Any?>().also { m -> obj { k -> m[k] = value() } }
            '[' -> ArrayList<Any?>().also { l -> list { l += value() } }
            '"' -> string()
            't', 'f' -> bool()
            'n' -> if (nullValue()) null else error("expected null at $i")
            else -> decimal()
        }

        fun bool(): Boolean = when {
            peek() == 't' && s.startsWith("true", i) -> { i += 4; true }
            peek() == 'f' && s.startsWith("false", i) -> { i += 5; false }
            else -> error("expected true or false at $i")
        }

        /** `null` (then true) or nothing read (false). */
        fun nullValue(): Boolean = if (peek() == 'n' && s.startsWith("null", i)) { i += 4; true } else false

        /** Calls [item] for each element of a list `[...]` (empty lists allowed). */
        fun list(item: () -> Unit) {
            expect('[')
            if (peek() == ']') { i++; return }
            do item() while (take(',', ']') == ',')
        }

        /** Calls [field] with each key of an object `{...}`, the reader placed on its value. */
        fun obj(field: (String) -> Unit) {
            expect('{')
            if (peek() == '}') { i++; return }
            do {
                val key = string()
                expect(':')
                field(key)
            } while (take(',', '}') == ',')
        }

        /** Throws if anything but spaces is left. */
        fun end() {
            if (peek() != null) error("text after the end at $i")
        }
    }
}
