package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BackupScheduleTest {
    private val file = File(Files.createTempDirectory("schedule").toFile(), "backup-state.json")

    @Test
    fun nothingStoredMeansNoFolderAndNoBackupYet() {
        val s = BackupSchedule(file)
        assertNull(s.folder)
        assertEquals(0L, s.lastAuto)
        assertEquals(0L, s.lastBackup)
        file.writeText("{")
        assertNull(BackupSchedule(file).folder) // damaged: as new
    }

    @Test
    fun itIsSavedAndReadBack() {
        val s = BackupSchedule(file)
        s.folder = "content://tree/primary%3ABackups"
        s.lastAuto = 5
        s.lastBackup = 9
        s.save()
        val t = BackupSchedule(file)
        assertEquals("content://tree/primary%3ABackups", t.folder)
        assertEquals(5L, t.lastAuto)
        assertEquals(9L, t.lastBackup)
        t.folder = null
        t.save()
        assertNull(BackupSchedule(file).folder)
    }

    @Test
    fun anAutomaticBackupIsDueOnlyWithAFolderAndADayAfterTheLast() {
        val s = BackupSchedule(file)
        assertFalse(s.due(BackupSchedule.DAY * 10))
        s.folder = "content://x"
        assertTrue(s.due(BackupSchedule.DAY * 10))
        s.lastAuto = BackupSchedule.DAY * 10
        assertFalse(s.due(BackupSchedule.DAY * 11 - 1))
        assertTrue(s.due(BackupSchedule.DAY * 11))
    }

    @Test
    fun theNamesAreOursAndOnlyOurOldestBeyondFiveAreDeleted() {
        val name = BackupSchedule.fileName(1_759_600_000_000L)
        assertTrue(name, BackupSchedule.OURS.matches(name))
        assertTrue(name.startsWith("GBA Emulator backup 20") && name.endsWith(".zip"))
        val ours = (1..7).map { "GBA Emulator backup 2026-10-0$it 10-00.zip" }
        val names = ours + listOf("GBA Emulator backup 2026-10-01 10-00 (1).zip", "holiday.jpg", "GBA Emulator backup notes.zip")
        assertEquals(listOf("GBA Emulator backup 2026-10-02 10-00.zip", "GBA Emulator backup 2026-10-01 10-00.zip",
            "GBA Emulator backup 2026-10-01 10-00 (1).zip").sorted(), BackupSchedule.toDelete(names.shuffled()).sorted())
    }
    @Test
    fun ofBackupsMadeInTheSameMinuteTheLaterNumbersAreTheNewer() {
        // Android names a second file of the same name "… (1).zip", then "(2)", … (found in Claude's check: a text sort
        // put "(1)" before the plain name, so the newer one was deleted)
        val minute = "GBA Emulator backup 2026-10-04 20-44"
        val names = listOf("$minute.zip") + (1..10).map { "$minute ($it).zip" }
        assertEquals(listOf("$minute.zip") + (1..5).map { "$minute ($it).zip" }, BackupSchedule.toDelete(names.shuffled()).sortedBy { names.indexOf(it) })
    }
}
