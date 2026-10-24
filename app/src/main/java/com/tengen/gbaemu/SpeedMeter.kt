package com.tengen.gbaemu

import java.util.Locale

/**
 * The Show speed label (Settings → Picture): "60 fps · 1.0×" from the frames the emulator ran in about the last
 * second — fast-forward shows more, rewinding (no frames run) less. [sample] it every so often with the time and
 * the frame count (NativeBridge.debugFrameCount); the label changes once a second.
 */
class SpeedMeter {
    private var lastNanos = 0L
    private var lastFrames = -1L

    /** null until a second has been measured. */
    var label: String? = null
        private set

    fun sample(nanos: Long, frames: Long): String? {
        if (lastFrames < 0 || frames < lastFrames) { // the first sample, or a new game (the count restarted)
            lastNanos = nanos
            lastFrames = frames
            return label
        }
        val elapsed = nanos - lastNanos
        if (elapsed < SECOND) return label
        val fps = (frames - lastFrames) * SECOND.toDouble() / elapsed
        label = String.format(Locale.US, "%.0f fps · %.1f×", fps, fps / GBA_FPS)
        lastNanos = nanos
        lastFrames = frames
        return label
    }

    companion object {
        const val GBA_FPS = 59.7275
        private const val SECOND = 1_000_000_000L
    }
}
