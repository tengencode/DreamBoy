package com.tengen.gbaemu

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.graphics.pdf.PdfRenderer
import android.os.ParcelFileDescriptor
import java.io.File
import java.io.IOException
import kotlin.math.roundToInt

/**
 * A Delta skin's picture as a bitmap at the size it is drawn (9): a PDF's first page drawn by Android's PdfRenderer
 * (sharp at any size, see-through where the PDF is empty), a PNG decoded. Slow (a PDF up to ~0.7 s): call it off the
 * UI thread. PDF or PNG is told by the file's first bytes, not its name.
 */
object SkinPicture {
    fun isPdf(file: File): Boolean = try {
        file.inputStream().use { s ->
            val head = ByteArray(4)
            s.read(head) == 4 && String(head, Charsets.ISO_8859_1) == "%PDF"
        }
    } catch (e: IOException) {
        false
    }

    /** [file] at [w] × [h]: a PDF exactly, a PNG decoded no smaller than that (halved while it stays big enough); null if unreadable. */
    fun make(file: File, w: Int, h: Int): Bitmap? = try {
        if (isPdf(file)) pdf(file, w, h) else png(file, w, h)
    } catch (e: Exception) { // damaged, or too big for memory
        null
    }

    /** As [make]; a PDF's drawing is kept beside it (`cache-<p|l>-<w>x<h>.png`, one per way round) and read from there next time. */
    fun kept(dir: File, image: String, sideways: Boolean, w: Int, h: Int): Bitmap? {
        val file = File(dir, image)
        if (!isPdf(file)) return make(file, w, h) // a PNG decodes faster than a kept copy would be written
        val prefix = "cache-${if (sideways) "l" else "p"}-"
        val copy = File(dir, "$prefix${w}x$h.png")
        if (copy.isFile) BitmapFactory.decodeFile(copy.path)?.let { return it }
        val bmp = make(file, w, h) ?: return null
        dir.listFiles { f -> f.name.startsWith(prefix) }?.forEach { it.delete() } // the copy for an older view size
        try {
            copy.outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        } catch (e: IOException) {
            copy.delete()
        }
        return bmp
    }

    /** The Skin page's tile picture: [face]'s picture 240 px wide, as a PNG in [out]. */
    fun thumbnail(dir: File, face: DeltaSkin.Face, out: File) {
        val w = 240
        val h = (w * face.height / face.width).roundToInt().coerceAtLeast(1)
        val bmp = make(File(dir, face.image), w, h) ?: return
        val tile = if (bmp.width == w && bmp.height == h) bmp else Bitmap.createScaledBitmap(bmp, w, h, true)
        out.outputStream().use { tile.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    private fun pdf(file: File, w: Int, h: Int): Bitmap =
        ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY).use { fd ->
            PdfRenderer(fd).use { renderer ->
                renderer.openPage(0).use { page ->
                    val out = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
                    val m = Matrix().apply { setScale(w.toFloat() / page.width, h.toFloat() / page.height) }
                    page.render(out, null, m, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY)
                    out
                }
            }
        }

    private fun png(file: File, w: Int, h: Int): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(file.path, bounds)
        var sample = 1
        while (bounds.outWidth / (sample * 2) >= w && bounds.outHeight / (sample * 2) >= h) sample *= 2
        return BitmapFactory.decodeFile(file.path, BitmapFactory.Options().apply { inSampleSize = sample })
    }
}
