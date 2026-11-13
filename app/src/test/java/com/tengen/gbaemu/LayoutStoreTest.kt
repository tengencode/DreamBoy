package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LayoutStoreTest {
    @Test
    fun gameBoyLayoutsAreTheirOwnAndAnOldFileStillReads() {
        // what 1.0.0 wrote: only the GBA layouts
        val old = "{\n  \"upright\": [\n    {\"id\": \"a\", \"x\": 2500, \"y\": 9000, \"scale\": 15000}\n  ],\n  \"sideways\": []\n}\n"
        val read = LayoutStore.fromJson(old)
        assertEquals(mapOf(LayoutItem.A to Place(0.25f, 0.9f, 1.5f)), read.of(false))
        assertTrue(read.of(false, "gb").isEmpty() && read.of(true, "gb").isEmpty())
        assertEquals(old, LayoutStore.toJson(read)) // untouched until a Game Boy layout is edited
        val edited = read.with(true, mapOf(LayoutItem.B to Place(0.5f, 0.5f, 1f)), "gb")
        assertEquals(read.of(false), edited.of(false)) // the GBA layouts did not move
        assertTrue(edited.of(true).isEmpty())
        assertEquals(mapOf(LayoutItem.B to Place(0.5f, 0.5f, 1f)), edited.of(true, "gb"))
        assertEquals(edited, LayoutStore.fromJson(LayoutStore.toJson(edited)))
        val gba = edited.with(false, emptyMap())
        assertEquals(edited.of(true, "gb"), gba.of(true, "gb")) // and the other way round
    }

    @Test
    fun noFileMeansTheDefaults() {
        val dir = Files.createTempDirectory("layout").toFile()
        assertEquals(LayoutStore.Layouts(), LayoutStore(dir).load())
        dir.deleteRecursively()
    }

    @Test
    fun bothLayoutsAreSavedAndReadBack() {
        val dir = Files.createTempDirectory("layout").toFile()
        val store = LayoutStore(dir)
        val saved = LayoutStore.Layouts(
            upright = mapOf(LayoutItem.A to Place(0.25f, 0.9f, 1.5f), LayoutItem.PICTURE to Place(0.5f, 0.2f, 0.75f)),
            sideways = mapOf(LayoutItem.DPAD to Place(0.1f, 0.6f, 0.5f)),
        )
        assertTrue(store.save(saved))
        assertEquals(saved, LayoutStore(dir).load())
        dir.deleteRecursively()
    }

    @Test
    fun valuesOutOfRangeAreBroughtIn() {
        val dir = Files.createTempDirectory("layout").toFile()
        File(dir, "layout.json").writeText(
            "{\"upright\": [{\"id\": \"a\", \"x\": 15000, \"y\": -20, \"scale\": 90000}, {\"id\": \"nothing\", \"x\": 1, \"y\": 1, \"scale\": 1}], \"sideways\": []}")
        val l = LayoutStore(dir).load()
        assertEquals(mapOf(LayoutItem.A to Place(1f, 0f, 2f)), l.upright) // an unknown item is left out
        assertEquals(emptyMap<LayoutItem, Place>(), l.sideways)
        dir.deleteRecursively()
    }

    @Test
    fun aDamagedFileIsKeptAsBadAndTheDefaultsAreUsed() {
        val dir = Files.createTempDirectory("layout").toFile()
        File(dir, "layout.json").writeText("{\"upright\": [{\"id\": ")
        val store = LayoutStore(dir)
        assertEquals(LayoutStore.Layouts(), store.load())
        assertTrue(store.wasDamaged)
        assertTrue(File(dir, "layout.json.bad").isFile)
        assertFalse(File(dir, "layout.json").exists())
        dir.deleteRecursively()
    }
}
