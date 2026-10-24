package com.tengen.gbaemu

import java.io.File
import org.junit.Assert.assertTrue
import org.junit.Test

/** The app has its own icon (a cartridge) and launch theme (12), instead of Android's default robot. */
class AppIconTest {
    private fun read(path: String) = File("src/main/$path").readText() // tests run in app/

    @Test
    fun theManifestUsesTheCartridgeIconAndTheLaunchTheme() {
        val manifest = read("AndroidManifest.xml")
        for (attr in listOf("android:icon=\"@mipmap/ic_launcher\"", "android:roundIcon=\"@mipmap/ic_launcher_round\"",
            "android:theme=\"@style/Theme.GbaEmu\"")) assertTrue(attr, manifest.contains(attr))
    }

    @Test
    fun theIconHasEveryLayerAndTheThemeEveryVariant() {
        for (name in listOf("ic_launcher", "ic_launcher_round")) {
            val icon = read("res/mipmap-anydpi-v26/$name.xml")
            for (layer in listOf("background", "foreground", "monochrome")) {
                assertTrue("$name: $layer", icon.contains("@drawable/ic_launcher_$layer"))
                assertTrue(File("src/main/res/drawable/ic_launcher_$layer.xml").exists())
            }
        }
        for (dir in listOf("values", "values-night", "values-v31")) assertTrue(dir, read("res/$dir/themes.xml").contains("Theme.GbaEmu"))
        for (dir in listOf("values", "values-night")) assertTrue(dir, read("res/$dir/colors.xml").contains("name=\"plastic\""))
    }
}
