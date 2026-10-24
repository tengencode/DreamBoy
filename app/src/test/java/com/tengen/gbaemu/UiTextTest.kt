package com.tengen.gbaemu

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The words the app shows (milestone 8a): no "…" or "...", no "&", and no text symbols standing in for icons. Reads
 * every string literal in the app's Kotlin sources (comments are skipped), so a label can't slip back in.
 */
class UiTextTest {
    /** A string literal: its file, line and text (a template's code parts left out). */
    data class Literal(val file: String, val line: Int, val text: String)

    @Test
    fun noEllipsisAmpersandOrSymbolIconsInTheAppsText() {
        val bad = literals().flatMap { lit ->
            FORBIDDEN.filter { it in lit.text && (lit.file to it) !in ALLOWED }.map { "${lit.file}:${lit.line} \"${lit.text}\" has $it" }
        }
        assertEquals("", bad.joinToString("\n"))
    }

    @Test
    fun theReaderFindsLiteralsAndSkipsComments() {
        val q = QUOTE.toString()
        val src = "val a = ${q}one$q // ${q}not this…$q\n" +
            "/* ${q}nor this…$q */ val b = ${q}t ${DOLLAR}{if (c) ${q}in…$q else ${q}x$q} end$q\n" +
            "val r = $q$q${q}raw$q$q$q"
        val found = scan("x.kt", src)
        assertEquals(listOf("one", "t ", "in…", "x", " end", "raw"), found.map { it.text })
        assertEquals(listOf(1, 2, 2, 2, 2, 3), found.map { it.line })
    }

    private fun literals(): List<Literal> {
        val root = File("src/main/java")
        assertTrue("run from the app module (src/main/java not found)", root.isDirectory)
        return root.walkTopDown().filter { it.extension == "kt" }.flatMap { scan(it.name, it.readText()) }.toList()
    }

    companion object {
        val FORBIDDEN = listOf("…", "...", "&", "▶", "⋮", "☀", "⏩", "⟲")

        /** The touch buttons draw these on themselves (their look is milestone 8b's). */
        val ALLOWED = setOf("TouchLayout.kt" to "⏩", "TouchLayout.kt" to "⟲", "TouchControlsView.kt" to "⏩", "TouchControlsView.kt" to "⟲")

        // Written as codes: the reader is about these very characters.
        val NL = 10.toChar()
        val QUOTE = 34.toChar()
        val DOLLAR = 36.toChar()
        val APOS = 39.toChar()
        val BACKSLASH = 92.toChar()
        val TRIPLE = "$QUOTE$QUOTE$QUOTE"

        /** Every string literal's text in Kotlin [src]: plain and raw strings, templates' code read again for nested ones. */
        fun scan(file: String, src: String): List<Literal> = Scanner(file, src).run { code(inTemplate = false); out }
    }

    private class Scanner(val file: String, val src: String) {
        val out = mutableListOf<Literal>()
        var i = 0
        var line = 1

        /** Code, up to the unmatched closing brace that ends a template's code when [inTemplate]. */
        fun code(inTemplate: Boolean) {
            var depth = 0
            while (i < src.length) {
                val c = src[i]
                when {
                    c == NL -> { line++; i++ }
                    src.startsWith("//", i) -> while (i < src.length && src[i] != NL) i++
                    src.startsWith("/*", i) -> {
                        val end = src.indexOf("*/", i + 2).let { if (it < 0) src.length else it + 2 }
                        line += src.substring(i, end).count { it == NL }
                        i = end
                    }
                    c == APOS -> charLiteral()
                    c == QUOTE -> string()
                    c == '{' -> { depth++; i++ }
                    c == '}' -> {
                        i++
                        if (depth == 0 && inTemplate) return
                        depth--
                    }
                    else -> i++
                }
            }
        }

        fun charLiteral() {
            i++ // the opening quote
            i += if (src[i] == BACKSLASH) 2 else 1
            while (i < src.length && src[i] != APOS) i++ // the rest of a unicode escape
            i++
        }

        fun string() {
            val raw = src.startsWith(TRIPLE, i)
            i += if (raw) 3 else 1
            val text = StringBuilder()
            var startLine = line
            fun flush() {
                if (text.isNotEmpty()) out += Literal(file, startLine, text.toString())
                text.clear()
            }
            while (i < src.length) {
                val c = src[i]
                if (raw && src.startsWith(TRIPLE, i)) { i += 3; break }
                if (!raw && c == QUOTE) { i++; break }
                if (!raw && c == BACKSLASH) { text.append(src[i + 1]); i += 2; continue }
                if (c == DOLLAR && i + 1 < src.length && src[i + 1] == '{') {
                    flush()
                    i += 2
                    code(inTemplate = true)
                    startLine = line
                    continue
                }
                if (c == NL) line++
                text.append(c)
                i++
            }
            flush()
        }
    }
}
