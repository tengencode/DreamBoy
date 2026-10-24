package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NativeLibraryTest {
    @Test
    fun nativeLibraryLoads() {
        assertNotNull(NativeBridge) /* first use runs System.loadLibrary("gbaemu") */
    }

    @Test
    fun retroAchievementsIsBuiltIn() {
        assertTrue(NativeBridge.raUserAgentClause().startsWith("rcheevos/12.5"))
        assertEquals(0, NativeBridge.raLoginStatus()[0]) // nobody signed in
        assertFalse(NativeBridge.raHardcoreActive())
    }
}
