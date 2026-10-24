package com.tengen.gbaemu

import android.content.Context
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** A backup and a restore on the phone, with Android's real preference files. */
@RunWith(AndroidJUnit4::class)
class BackupDeviceTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val dir = File(context.filesDir, "test_backup")
    private val names = Backup.PREFS.associateWith { "test_backup_$it" }

    @After
    fun cleanUp() {
        dir.deleteRecursively()
        for (n in names.values) { // commit waits for PrefsStore's queued apply() writes, which would write the file again
            context.getSharedPreferences(n, Context.MODE_PRIVATE).edit().clear().commit()
            context.deleteSharedPreferences(n)
        }
    }

    @Test
    fun aBackupComesBackAfterChanges() {
        val prefs = names.mapValues { (_, n) -> PrefsStore(context.getSharedPreferences(n, Context.MODE_PRIVATE)) as IntStore }
        val restore = Restore(dir, prefs, BoxArt(File(dir, "boxart")))
        File(dir, "saves").mkdirs()
        File(dir, "saves/e86b3224.sav").writeText("save")
        prefs.getValue("settings").put("smooth", 1)
        prefs.getValue("keys").put("k", 42)
        val bytes = ByteArrayOutputStream().also { restore.backUp(it, "0.1", "test", 1) }.toByteArray()
        File(dir, "saves/e86b3224.sav").writeText("changed")
        prefs.getValue("settings").put("smooth", 0)
        prefs.getValue("keys").put("other", 1)
        assertTrue(restore.check(ByteArrayInputStream(bytes)) != null)
        assertTrue(restore.apply("0.1", "test", 2).ok)
        assertEquals("save", File(dir, "saves/e86b3224.sav").readText())
        assertEquals(mapOf("smooth" to 1), prefs.getValue("settings").all())
        assertEquals(mapOf("k" to 42), prefs.getValue("keys").all())
    }
}
