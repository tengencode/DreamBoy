package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The core built for Android must give exactly the PC's approved picture and sound fingerprints. */
@RunWith(AndroidJUnit4::class)
class FingerprintTest {
    @Test
    fun androidCoreReproducesEveryApprovedFingerprint() {
        val entries = JSONObject(TestRoms.text("expected_frames.json")).getJSONArray("frames")
        assertEquals("approved entries", 32, entries.length())
        val failures = mutableListOf<String>()
        for (i in 0 until entries.length()) {
            val e = entries.getJSONObject(i)
            val got = NativeBridge.debugRunFrames(TestRoms.rom(e.getString("rom")), e.getInt("frames"))
            val parts = got?.split(" ") ?: listOf("load-failed", "")
            val frameOk = parts[0] == e.getString("hash")
            val audioOk = !e.has("audio") || parts[1] == e.getString("audio")
            if (!frameOk || !audioOk) failures += "${e.getString("name")}: got $got"
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }
}
