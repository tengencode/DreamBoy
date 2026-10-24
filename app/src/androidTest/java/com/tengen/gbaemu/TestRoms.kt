package com.tengen.gbaemu

import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.nio.ByteBuffer

/** The local test ROMs and tests/expected_frames.json, packed into the test APK's assets by the build. */
object TestRoms {
    private val assets get() = InstrumentationRegistry.getInstrumentation().context.assets

    fun text(path: String): String = assets.open(path).bufferedReader().use { it.readText() }

    /** A ROM by its path in expected_frames.json (relative to tests/roms, e.g. "tonc/m7_demo.gba"). */
    fun rom(romPath: String): ByteBuffer {
        val path = File("roms", romPath).normalize().path.replace('\\', '/')
        val bytes = assets.open(path).use { it.readBytes() }
        return ByteBuffer.allocateDirect(bytes.size).put(bytes).also { it.flip() }
    }
}
