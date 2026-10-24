package com.tengen.gbaemu

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.ByteArrayOutputStream

/** Box art pictures (10b): any picture shrunk to fit [SIZE] × [SIZE] (its shape kept), as a JPEG. */
object BoxPicture {
    const val SIZE = 400

    /** [bytes] (PNG, JPEG, …) as a JPEG no bigger than [SIZE] × [SIZE]; null if Android can't read the picture. */
    fun shrink(bytes: ByteArray): ByteArray? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
        var sample = 1 // decoded no bigger than needed: the longer side stays at least SIZE
        while (maxOf(bounds.outWidth, bounds.outHeight) / (sample * 2) >= SIZE) sample *= 2
        val full = BitmapFactory.decodeByteArray(bytes, 0, bytes.size, BitmapFactory.Options().apply { inSampleSize = sample })
            ?: return null
        val scale = minOf(1f, SIZE.toFloat() / maxOf(full.width, full.height))
        val w = maxOf(1, Math.round(full.width * scale))
        val h = maxOf(1, Math.round(full.height * scale))
        val small = if (w == full.width && h == full.height) full else Bitmap.createScaledBitmap(full, w, h, true)
        return ByteArrayOutputStream().also { small.compress(Bitmap.CompressFormat.JPEG, 90, it) }.toByteArray()
    }
}
