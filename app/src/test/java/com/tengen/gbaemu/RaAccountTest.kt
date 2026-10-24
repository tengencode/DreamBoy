package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RaAccountTest {
    private val dir: File = Files.createTempDirectory("ra").toFile()
    private val file = File(dir, "ra.json")

    @Test
    fun theNameAndTokenAreKeptAndReadBack() {
        val a = RaAccount(file)
        assertNull(a.load())
        assertTrue(a.save("Tester", "TOKEN"))
        assertEquals("Tester" to "TOKEN", RaAccount(file).load())
        assertFalse(File(dir, "ra.json.tmp").exists())
    }

    @Test
    fun clearForgetsItAndADamagedFileIsNoSignIn() {
        val a = RaAccount(file)
        a.save("Tester", "TOKEN")
        a.clear()
        assertNull(a.load())
        assertFalse(file.exists())
        file.writeText("{\"user\": \"Tester\"")
        assertNull(a.load())
        file.writeText("{\"user\": \"\", \"token\": \"T\"}")
        assertNull(a.load())
    }
}
