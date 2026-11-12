package com.tengen.gbaemu

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AboutTextTest {
    @Test
    fun aDocumentBecomesHeadingsParagraphsBulletsAndNotes() {
        val text = "# Title\n\n_Last updated: today._\n\nOne line\nand its next line.\n\n## Part\n\n- a point\n  that goes on\n- **bold** point\n\n### Small\n\nEnd.\n"
        assertEquals(listOf(
            DocBlock(DocKind.TITLE, "Title"),
            DocBlock(DocKind.NOTE, "Last updated: today."),
            DocBlock(DocKind.TEXT, "One line and its next line."),
            DocBlock(DocKind.HEADING, "Part"),
            DocBlock(DocKind.BULLET, "a point that goes on"),
            DocBlock(DocKind.BULLET, "**bold** point"),
            DocBlock(DocKind.SUBHEADING, "Small"),
            DocBlock(DocKind.TEXT, "End."),
        ), docBlocks(text))
    }

    @Test
    fun aLicencesLinesAreJoinedWithinEachParagraph() {
        assertEquals("MIT License\n\nPermission is granted to any person.\n\nTHE END",
            reflow("MIT License\r\n\r\nPermission is granted\n   to any person.\n  \nTHE END\n"))
    }

    @Test
    fun boldIsMarkedAndItsStarsRemoved() {
        assertEquals(listOf("plain " to false, "bold" to true, " after" to false), boldRuns("plain **bold** after"))
        assertEquals(listOf("no stars" to false), boldRuns("no stars"))
    }

    @Test
    fun thePrivacyPolicyHasNoMarkupLeftOnceRead() {
        // JVM tests run in app/
        val blocks = docBlocks(File("../PRIVACY.md").readText())
        val shown = blocks.flatMap { b -> boldRuns(b.text).map { it.first } }
        assertTrue(shown.none { "#" in it || "**" in it || it.startsWith("_") || it.startsWith("- ") })
        assertTrue(blocks.any { it.kind == DocKind.HEADING && it.text == "Contact" })
    }
}
