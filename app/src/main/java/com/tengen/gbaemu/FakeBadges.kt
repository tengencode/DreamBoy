package com.tengen.gbaemu

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import java.io.ByteArrayOutputStream

/** Badge pictures for the pretend server (14): a coloured square with the badge's number; grey for a locked one. */
object FakeBadges {
    fun png(url: String): ByteArray {
        val name = url.substringAfterLast('/').substringBefore(".png")
        val locked = name.endsWith("_lock")
        val number = name.removeSuffix("_lock").trimStart('0').ifEmpty { "0" }
        val bmp = Bitmap.createBitmap(64, 64, Bitmap.Config.ARGB_8888)
        val c = Canvas(bmp)
        val hue = (number.hashCode() and 0xFF) * 360f / 256f
        c.drawColor(if (locked) Color.rgb(110, 110, 110) else Color.HSVToColor(floatArrayOf(hue, 0.6f, 0.85f)))
        val p = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; textSize = 28f; textAlign = Paint.Align.CENTER }
        c.drawText(number, 32f, 42f, p)
        return ByteArrayOutputStream().also { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }.toByteArray()
    }
}
