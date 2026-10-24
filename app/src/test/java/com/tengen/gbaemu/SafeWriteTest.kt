package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SafeWriteTest {
    private val dir = Files.createTempDirectory("safe").toFile()

    @Test
    fun writesANewFileAndReplacesAnOldOneLeavingNoTemporaryFile() {
        val f = File(dir, "a/b/x.json") // folders made as needed
        assertTrue(f.writeSafely("one".toByteArray()))
        assertTrue(f.writeSafely("two".toByteArray()))
        assertArrayEquals("two".toByteArray(), f.readBytes())
        assertFalse(File(f.path + ".tmp").exists())
    }

    @Test
    fun saysWhenItCantWrite() {
        val notAFolder = File(dir, "file").apply { writeText("x") }
        assertFalse(File(notAFolder, "x.json").writeSafely("one".toByteArray()))
    }
}
