package com.tengen.gbaemu

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LicencesTest {
    // The JVM tests run in app/.
    private val gradle = File("build.gradle.kts")

    @Test
    fun everyShippedLibraryIsListed() {
        assertTrue("run from the app module", gradle.isFile)
        val groups = Regex("""^\s*implementation\("([^:"]+):""", RegexOption.MULTILINE).findAll(gradle.readText()).map { it.groupValues[1] }.toList()
        assertTrue(groups.isNotEmpty())
        val unlisted = groups.filter { g -> Licences.SHIPPED.none { l -> l.groups.any { g.startsWith(it) } } }
        assertEquals(emptyList<String>(), unlisted)
    }

    @Test
    fun everyLicenceTextIsInTheApp() {
        val made = setOf("licences/gpl-3.0.txt", "licences/rcheevos-mit.txt") // copied in at build time
        val missing = Licences.SHIPPED.map { it.asset }.filter { it !in made && !File("src/main/assets/${it}").isFile }
        assertEquals(emptyList<String>(), missing)
    }

    @Test
    fun thePrivacyPolicyHasNoPlaceholders() {
        val text = File("../PRIVACY.md").readText()
        for (bad in listOf("TODO", "TBD", "[", "example.com")) assertTrue("PRIVACY.md has $bad", bad !in text)
        for (must in listOf("retroachievements.org", "thumbnails.libretro.com", "raw.githubusercontent.com", "api.github.com",
                "https://github.com/tengencode/DreamBoy/issues")) assertTrue("PRIVACY.md lacks $must", must in text)
    }
}
