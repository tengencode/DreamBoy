package com.tengen.gbaemu

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document

/** The automatic backups' folder (11): a folder the user picked (Android's document tree); every call may throw. */
class BackupFolder(private val resolver: ContentResolver, private val tree: Uri) {
    private val folder: Uri = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))

    /** The folder's name as the picker showed it. */
    fun name(): String? = resolver.query(folder, arrayOf(Document.COLUMN_DISPLAY_NAME), null, null, null)?.use {
        if (it.moveToFirst()) it.getString(0) else null
    }

    /** A new, empty `.zip` called [name] in the folder. */
    fun create(name: String): Uri? = DocumentsContract.createDocument(resolver, folder, "application/zip", name)

    /** The folder's files: name and address. */
    fun list(): List<Pair<String, Uri>> {
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
        return resolver.query(children, arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME), null, null, null)?.use { c ->
            val out = mutableListOf<Pair<String, Uri>>()
            while (c.moveToNext()) out += c.getString(1) to DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0))
            out
        }.orEmpty()
    }

    fun delete(doc: Uri): Boolean = try {
        DocumentsContract.deleteDocument(resolver, doc)
    } catch (e: Exception) {
        false
    }
}
