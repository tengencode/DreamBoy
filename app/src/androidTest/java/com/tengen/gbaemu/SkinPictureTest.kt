package com.tengen.gbaemu

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.Paint
import android.graphics.pdf.PdfDocument
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** A Delta skin's pictures (9): a PDF drawn by Android's PdfRenderer, a PNG decoded, the tile picture. */
@RunWith(AndroidJUnit4::class)
class SkinPictureTest {
    private val dir = File(InstrumentationRegistry.getInstrumentation().targetContext.cacheDir, "skinpicture").apply {
        deleteRecursively()
        mkdirs()
    }

    private fun png(name: String, w: Int, h: Int, colour: Int) {
        val b = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888).apply { eraseColor(colour) }
        File(dir, name).outputStream().use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    @Test
    fun aPdfIsDrawnAtTheAskedSizeSeeThroughWhereEmptyAndKept() {
        val pdf = File(dir, "p.pdf")
        PdfDocument().apply {
            val page = startPage(PdfDocument.PageInfo.Builder(100, 50, 1).create())
            page.canvas.drawRect(0f, 0f, 50f, 50f, Paint().apply { color = Color.RED }) // the left half red, the right empty
            finishPage(page)
            pdf.outputStream().use { writeTo(it) }
            close()
        }
        assertTrue(SkinPicture.isPdf(pdf))
        val bmp = SkinPicture.kept(dir, "p.pdf", sideways = false, 300, 150)!!
        assertEquals(300, bmp.width)
        assertEquals(150, bmp.height)
        assertEquals(Color.RED, bmp.getPixel(60, 75))
        assertEquals(0, Color.alpha(bmp.getPixel(240, 75)))
        val copy = File(dir, "cache-p-300x150.png")
        assertTrue(copy.isFile)
        assertEquals(Color.RED, BitmapFactory.decodeFile(copy.path).getPixel(60, 75))
        SkinPicture.kept(dir, "p.pdf", sideways = true, 150, 300) // the other way round: its own copy
        SkinPicture.kept(dir, "p.pdf", sideways = false, 400, 200) // a new size (split screen, display size)
        assertFalse("the old copy for this way round is kept", copy.exists())
        assertTrue(File(dir, "cache-p-400x200.png").isFile)
        assertTrue(File(dir, "cache-l-150x300.png").isFile)
    }

    @Test
    fun aPngIsDecodedNoSmallerThanNeededAndNotKept() {
        png("p.png", 800, 400, Color.BLUE)
        assertFalse(SkinPicture.isPdf(File(dir, "p.png")))
        val bmp = SkinPicture.kept(dir, "p.png", sideways = true, 300, 150)!!
        assertEquals(400, bmp.width) // 800 halved once; halved again (200) it would be smaller than 300
        assertEquals(Color.BLUE, bmp.getPixel(10, 10))
        assertFalse(File(dir, "cache-l-300x150.png").exists())
    }

    @Test
    fun theTilePictureIs240Wide() {
        png("p.png", 800, 400, Color.GREEN)
        val out = File(dir, "thumb.png")
        SkinPicture.thumbnail(dir, DeltaSkin.Face("p.png", 400f, 200f, emptyList(), null, false), out)
        val t = BitmapFactory.decodeFile(out.path)
        assertEquals(240, t.width)
        assertEquals(120, t.height)
        assertEquals(Color.GREEN, t.getPixel(100, 60))
    }
}
