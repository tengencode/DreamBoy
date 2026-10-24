package com.tengen.gbaemu

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Color
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.ByteArrayOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Box art is shrunk to fit 400 × 400 with its shape kept, as a JPEG (10b). */
@RunWith(AndroidJUnit4::class)
class BoxPictureTest {
    private fun png(w: Int, h: Int): ByteArray {
        val b = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888).apply { eraseColor(Color.RED) }
        return ByteArrayOutputStream().also { b.compress(Bitmap.CompressFormat.PNG, 100, it) }.toByteArray()
    }

    @Test
    fun aBigPictureIsShrunkToFitKeepingItsShape() {
        val jpeg = BoxPicture.shrink(png(1000, 800))!!
        assertEquals(0xFF.toByte(), jpeg[0]) // JPEG starts FF D8
        assertEquals(0xD8.toByte(), jpeg[1])
        val b = BitmapFactory.decodeByteArray(jpeg, 0, jpeg.size)
        assertEquals(400, b.width)
        assertEquals(320, b.height)
        val c = b.getPixel(200, 160)
        assertTrue(Color.red(c) > 240 && Color.green(c) < 20 && Color.blue(c) < 20)
    }

    @Test
    fun aSmallPictureKeepsItsSizeAndNonsenseIsRefused() {
        val b = BoxPicture.shrink(png(200, 100))!!.let { BitmapFactory.decodeByteArray(it, 0, it.size) }
        assertEquals(200, b.width)
        assertEquals(100, b.height)
        assertNull(BoxPicture.shrink(ByteArray(10)))
    }
}
