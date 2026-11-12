import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// Release signing: the key's details in the gitignored keystore.properties at the repo root (storeFile, storePassword,
// keyAlias, keyPassword). Without it a release build is unsigned. Release builds are never debuggable.
val releaseKey = rootProject.file("keystore.properties").takeIf { it.isFile }?.let { f ->
    Properties().apply { f.inputStream().use { load(it) } }
}

android {
    namespace = "com.tengen.gbaemu"
    compileSdk = 36
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.tengen.dreamboy"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1.0.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += listOf("x86_64", "arm64-v8a") }
        // -PgbaForceGeneric=ON: the arm64 core with only the plain-C twins of its fast paths (tools/dev.sh GENERIC=1)
        externalNativeBuild { cmake { arguments += "-DGBA_FORCE_GENERIC=${project.findProperty("gbaForceGeneric") ?: "OFF"}" } }
    }

    externalNativeBuild {
        cmake {
            path = file("../platform/android/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    signingConfigs {
        if (releaseKey != null) create("release") {
            storeFile = rootProject.file(releaseKey.getProperty("storeFile"))
            storePassword = releaseKey.getProperty("storePassword")
            keyAlias = releaseKey.getProperty("keyAlias")
            keyPassword = releaseKey.getProperty("keyPassword")
        }
    }
    buildTypes {
        // Its own id, so a debug install never clashes with the release (signed with another key) on the same phone.
        getByName("debug") {
            applicationIdSuffix = ".debug"
            manifestPlaceholders["appLabel"] = "DreamBoy debug"
        }
        getByName("release") {
            manifestPlaceholders["appLabel"] = "DreamBoy"
            signingConfig = signingConfigs.findByName("release")
        }
    }

    buildFeatures { compose = true }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // On-device tests read the local test ROMs + approved fingerprints from the test APK's assets
    // (copied from the gitignored tests/ folders at build time; never in the app APK or in git).
    sourceSets.getByName("androidTest").assets.srcDir(layout.buildDirectory.dir("generated/testRoms").get().asFile)
}

kotlin {
    compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) }
}

val copyTestRoms by tasks.registering(Copy::class) {
    from(rootProject.file("tests")) {
        include("expected_frames.json", "roms/jsmolka/*.gba", "roms/tonc/*.gba", "own_roms/*.gba")
    }
    into(layout.buildDirectory.dir("generated/testRoms"))
}
tasks.matching { it.name == "mergeDebugAndroidTestAssets" }.configureEach { dependsOn(copyTestRoms) }

dependencies {
    implementation("androidx.core:core-ktx:1.17.0")
    implementation("androidx.activity:activity-compose:1.12.3")
    implementation("androidx.compose.ui:ui-android:1.11.4")
    implementation("androidx.compose.foundation:foundation-android:1.11.4")
    implementation("androidx.compose.material3:material3-android:1.4.0")
    implementation("androidx.compose.material:material-icons-extended-android:1.7.8") // the menus' icons (8a)
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("androidx.compose.ui:ui-test-junit4-android:1.11.4") // screen tests (8a)
    androidTestImplementation("androidx.test.espresso:espresso-core:3.7.0") // 3.5 (the default) fails on Android 14+
    debugImplementation("androidx.compose.ui:ui-test-manifest:1.11.4")
}
