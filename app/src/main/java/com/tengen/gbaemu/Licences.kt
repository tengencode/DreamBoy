package com.tengen.gbaemu

/** The project's page: the About page's link, the privacy policy's contact (16). */
const val GITHUB_URL = "https://github.com/tengencode/DreamBoy"

/**
 * A part shipped in the app and its licence (16); [asset] is the licence text in the app's assets, [groups] the Maven
 * group prefixes it covers (LicencesTest checks every dependency in build.gradle.kts is listed).
 */
data class Licence(val part: String, val licence: String, val asset: String, val groups: List<String>)

object Licences {
    const val PRIVACY_ASSET = "privacy.md"

    val SHIPPED = listOf(
        Licence("DreamBoy", "GPL 3.0 or later", "licences/gpl-3.0.txt", emptyList()),
        Licence("rcheevos 12.5.0 (RetroAchievements)", "MIT", "licences/rcheevos-mit.txt", emptyList()),
        Licence("Jetpack Compose, Material 3 and Material icons", "Apache 2.0", "licences/apache-2.0.txt", listOf("androidx.compose.")),
        Licence("AndroidX core, activity and lifecycle", "Apache 2.0", "licences/apache-2.0.txt", listOf("androidx.")),
        Licence("Kotlin standard library and kotlinx coroutines", "Apache 2.0", "licences/apache-2.0.txt",
            listOf("org.jetbrains.kotlin", "org.jetbrains.kotlinx")),
    )
}
