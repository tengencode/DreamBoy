package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class BackupTextTest {
    @Test
    fun theBackupRowSaysTheLastBackupAndAutomatic() {
        assertEquals("No backup yet", backupSummary(BackupStatus()))
        assertEquals("No backup yet · automatic", backupSummary(BackupStatus(auto = true)))
        val today = backupSummary(BackupStatus(lastBackup = System.currentTimeMillis()))
        assertTrue(today, today.startsWith("Last backup today "))
    }

    @Test
    fun theRestoreQuestionCountsWhatIsInTheBackup() {
        val many = Backup.Info(1, "0.1", 0, "phone A", games = 6, states = 23, cheats = 4, skins = 2)
        assertEquals("Made on phone A, 4 Oct 2026 14:02: 6 games, 23 save states, settings, cheats, 2 skins. " +
            "Everything on this phone is replaced; what's here now is kept as a safety copy.", restoreText(many, "4 Oct 2026 14:02"))
        val one = Backup.Info(1, "0.1", 0, "Pixel 9", games = 1, states = 1, cheats = 0, skins = 1)
        assertEquals("Made on Pixel 9, today: 1 game, 1 save state, settings, 1 skin. " +
            "Everything on this phone is replaced; what's here now is kept as a safety copy.", restoreText(one, "today"))
    }
}
