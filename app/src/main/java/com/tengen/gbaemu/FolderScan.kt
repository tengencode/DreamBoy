package com.tengen.gbaemu

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document

/**
 * The games in the games folder and its subfolders, through the Storage Access Framework (the folder's tree
 * permission is kept). Slow on big folders: call it off the main thread.
 */
object FolderScan {
    private const val MAX_DEPTH = 8

    /** Whether each zip holds a game, by key and size: a zip without one is read to its end, so read it once.
     *  shortcut: kept only while the app runs, read again after a restart. */
    private val zipHasGame = java.util.concurrent.ConcurrentHashMap<String, Boolean>()

    /** How the library matches a file however it was reached: its provider and document id. */
    fun keyOf(uri: Uri): String = "${uri.authority}/${DocumentsContract.getDocumentId(uri)}"

    /** The folder's name as the Files app shows it, or null if the folder can't be opened. */
    fun folderName(resolver: ContentResolver, tree: Uri): String? = try {
        val doc = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
        resolver.query(doc, arrayOf(Document.COLUMN_DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        }
    } catch (e: Exception) {
        null
    }

    /** The game files (`.gba`, `.gb`, `.gbc`) and the `.zip` files holding one; null if the folder can't be opened (gone, no permission). */
    fun scan(resolver: ContentResolver, tree: Uri): List<FoundGame>? = try {
        val out = mutableListOf<FoundGame>()
        walk(resolver, tree, DocumentsContract.getTreeDocumentId(tree), 0, out, top = true)
        out
    } catch (e: Exception) {
        null
    }

    private fun walk(resolver: ContentResolver, tree: Uri, parent: String, depth: Int, out: MutableList<FoundGame>, top: Boolean) {
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent)
        val cols = arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
            Document.COLUMN_SIZE)
        val cursor = resolver.query(children, cols, null, null, null)
        if (cursor == null) {
            if (top) error("the folder can't be listed") // a subfolder that can't be listed is just skipped
            return
        }
        cursor.use {
            while (it.moveToNext()) {
                val id = it.getString(0) ?: continue
                val name = it.getString(1) ?: continue
                if (it.getString(2) == Document.MIME_TYPE_DIR) {
                    if (depth < MAX_DEPTH) walk(resolver, tree, id, depth + 1, out, top = false)
                    continue
                }
                if (!Library.isGameFile(name)) continue
                val uri = DocumentsContract.buildDocumentUriUsingTree(tree, id)
                val size = if (it.isNull(3)) 0L else it.getLong(3)
                if (name.endsWith(".zip", ignoreCase = true)) {
                    val known = "${keyOf(uri)} $size"
                    val ok = zipHasGame[known] ?: try { // a zip that can't be read now is tried again next time
                        (resolver.openInputStream(uri)?.use(RomFile::zipHasGame) ?: false).also { has -> zipHasGame[known] = has }
                    } catch (e: Exception) {
                        false
                    }
                    if (!ok) continue
                }
                out += FoundGame(keyOf(uri), uri.toString(), Library.nameOf(name), size)
            }
        }
    }
}
