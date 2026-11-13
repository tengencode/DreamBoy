package com.tengen.gbaemu

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class SkinFilesTest {
    @get:Rule
    val tmp = TemporaryFolder()

    private val thumbs = ArrayList<String>()

    private fun info(id: String = "com.test.one", name: String = "One", type: String = DeltaSkin.GBA, image: String = "p.png") =
        """{"name": "$name", "identifier": "$id", "gameTypeIdentifier": "$type", "representations": {"iphone": {"edgeToEdge":
        {"portrait": {"assets": {"small": "$image"}, "mappingSize": {"width": 320, "height": 240},
        "items": [{"inputs": ["a"], "frame": {"x": 1, "y": 2, "width": 3, "height": 4}}]}}}}}"""

    private fun zip(vararg entries: Pair<String, String>): InputStream {
        val out = ByteArrayOutputStream()
        ZipOutputStream(out).use { z ->
            for ((name, text) in entries) {
                z.putNextEntry(ZipEntry(name))
                z.write(text.toByteArray())
                z.closeEntry()
            }
        }
        return ByteArrayInputStream(out.toByteArray())
    }

    /** The tile picture is made by Android's drawing in the app; here a stand-in writes a text. */
    private fun files(maxBytes: Long = SkinFiles.MAX_BYTES) = SkinFiles(tmp.root, { dir, face, out ->
        thumbs += face.image
        out.writeText("tile of " + File(dir, face.image).readText())
    }, maxBytes)

    private fun problem(f: SkinFiles, input: InputStream): SkinProblem? = try {
        f.import(input)
        null
    } catch (e: SkinException) {
        e.problem
    }

    @Test
    fun aGoodSkinIsStoredAsNumberOneWithItsTilePicture() {
        val f = files()
        val s = f.import(zip("info.json" to info(), "p.png" to "picture"))
        assertEquals(1, s.number)
        assertEquals(File(tmp.root, "skins/1"), s.dir)
        assertEquals("picture", File(s.dir, "p.png").readText())
        assertEquals("tile of picture", s.thumb.readText())
        assertEquals(listOf("p.png"), thumbs)
        assertEquals(listOf("One"), f.list().map { it.skin.name })
        assertEquals("One", f.get(1)!!.skin.name)
        assertFalse(File(tmp.root, "skins/.incoming").exists())
    }

    @Test
    fun infoJsonInsideOneTopFolderWithAMacFolderBeside() {
        val s = files().import(zip("My Skin/info.json" to info(), "My Skin/p.png" to "picture", "__MACOSX/My Skin/._p.png" to "x"))
        assertTrue(File(s.dir, "info.json").isFile)
        assertTrue(File(s.dir, "p.png").isFile)
    }

    @Test
    fun theSameSkinAgainReplacesItsCopyAnotherGetsTheNextNumber() {
        val f = files()
        assertEquals(1, f.import(zip("info.json" to info(), "p.png" to "a")).number)
        assertEquals(2, f.import(zip("info.json" to info(id = "com.test.two", name = "Two"), "p.png" to "b")).number)
        val again = f.import(zip("info.json" to info(name = "One v2"), "p.png" to "c"))
        assertEquals(1, again.number)
        assertEquals("c", File(again.dir, "p.png").readText())
        assertEquals(listOf("One v2", "Two"), f.list().map { it.skin.name })
    }

    @Test
    fun removeDeletesTheSkinsFolder() {
        val f = files()
        val s = f.import(zip("info.json" to info(), "p.png" to "a"))
        f.remove(s.number)
        assertFalse(s.dir.exists())
        assertEquals(emptyList<SkinFiles.Imported>(), f.list())
        assertNull(f.get(1))
    }

    @Test
    fun eachProblemLeavesNothingBehind() {
        val f = files(maxBytes = 1000)
        assertEquals(SkinProblem.NOT_A_SKIN, problem(f, ByteArrayInputStream("not a zip at all".toByteArray())))
        assertEquals(SkinProblem.NOT_A_SKIN, problem(f, zip("readme.txt" to "hi")))
        assertEquals(SkinProblem.NOT_A_SKIN, problem(f, zip("info.json" to "{ broken")))
        assertEquals(SkinProblem.OTHER_CONSOLE, problem(f, zip("info.json" to info(type = "com.rileytestut.delta.game.nes"), "p.png" to "a")))
        assertEquals(SkinProblem.PICTURE_MISSING, problem(f, zip("info.json" to info())))
        assertEquals(SkinProblem.NOT_A_SKIN, problem(f, zip("info.json" to info(image = "../p.png"), "p.png" to "a")))
        assertEquals(SkinProblem.NOT_A_SKIN, problem(f, zip("info.json" to info(), "p.png" to "a", "../evil.txt" to "x")))
        assertEquals(SkinProblem.TOO_BIG, problem(f, zip("info.json" to info(), "p.png" to "x".repeat(2000))))
        assertEquals(SkinProblem.TOO_BIG, problem(f, zip(*Array(SkinFiles.MAX_FILES + 1) { "f$it.txt" to "" })))
        assertEquals(emptyList<SkinFiles.Imported>(), f.list())
        assertFalse(File(tmp.root, "skins/.incoming").exists())
        assertFalse(File(tmp.root, "skins/evil.txt").exists())
        assertFalse(File(tmp.root, "evil.txt").exists())
    }

    @Test
    fun anUnreadableFolderIsLeftOutAndItsNumberIsNotReused() {
        File(tmp.root, "skins/7").mkdirs()
        File(tmp.root, "skins/7/info.json").writeText("{ broken")
        val f = files()
        assertEquals(emptyList<SkinFiles.Imported>(), f.list())
        assertNull(f.get(7))
        assertEquals(8, f.import(zip("info.json" to info(), "p.png" to "a")).number)
    }

    @Test
    fun aMissingSkinClearsOnlyItsOwnFamily() {
        val f = files()
        val gba = f.import(zip("info.json" to info(), "p.png" to "a"))
        val s = Settings(skinImport = gba.number, skinImportGb = 9) // 9: its folder is gone
        assertEquals(gba.number, chosenSkin(s, "gba", f.list())?.number)
        assertNull(chosenSkin(s, "gb", f.list()))
        assertNull(chosenSkin(Settings(), "gba", f.list())) // none chosen
    }

    @Test
    fun aSkinOfTheOtherFamilyIsNotUsed() {
        val f = files()
        val gb = f.import(zip("info.json" to info(type = DeltaSkin.GBC), "p.png" to "a"))
        assertEquals("gb", gb.skin.family)
        assertNull(chosenSkin(Settings(skinImport = gb.number), "gba", f.list()))
        assertEquals(gb.number, chosenSkin(Settings(skinImportGb = gb.number), "gb", f.list())?.number)
    }
}
