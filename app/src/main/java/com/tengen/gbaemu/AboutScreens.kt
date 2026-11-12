package com.tengen.gbaemu

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.PrivacyTip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** Settings → About DreamBoy (16): the name on a label band, the version, what it is, and its three links. */
@Composable
fun AboutPage(version: String, onGithub: () -> Unit, onPage: (SettingsPage) -> Unit) {
    Group {
        Column(Modifier.padding(16.dp)) {
            LabelBand("DreamBoy", big = true)
            Spacer(Modifier.height(14.dp))
            Text("Version $version", style = MaterialTheme.typography.bodySmall.merge(Tabular),
                color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(4.dp))
            Text("A Game Boy Advance emulator for Android. Game Boy and Game Boy Color support is planned.",
                style = MaterialTheme.typography.bodyLarge)
            Spacer(Modifier.height(8.dp))
            Text("Free software under the GPL 3.0 or later. No ads and no tracking.", style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
    Group {
        NavRow(Icons.Filled.Code, "GitHub page", "The code, new versions and bug reports", onClick = onGithub)
        NavRow(Icons.Filled.Description, "Open-source licences", "The parts DreamBoy is built from") { onPage(SettingsPage.LICENCES) }
        NavRow(Icons.Filled.PrivacyTip, "Privacy policy", "What stays on your phone and what doesn't", last = true) {
            onPage(SettingsPage.PRIVACY)
        }
    }
}

/** Settings → About → Open-source licences: every shipped part and its licence; tapping one shows its text. */
@Composable
fun LicencesPage(onOpen: (Int) -> Unit) {
    Group {
        Licences.SHIPPED.forEachIndexed { i, l ->
            NavRow(null, l.part, l.licence, last = i == Licences.SHIPPED.lastIndex) { onOpen(i) }
        }
    }
    Help("Each part's licence text is in the app.")
}

/** One licence's full text, as shipped in the app's assets. */
@Composable
fun LicenceText(licence: Licence) {
    val text = reflow(assetText(licence.asset))
    Group {
        Text(text, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(16.dp))
    }
}

/** The privacy policy (PRIVACY.md, shipped in the assets) as headings, paragraphs and points. */
@Composable
fun PrivacyPage() {
    val text = assetText(Licences.PRIVACY_ASSET)
    val blocks = remember(text) { docBlocks(text) }
    Group {
        Column(Modifier.padding(horizontal = 16.dp, vertical = 12.dp)) {
            for (b in blocks) when (b.kind) {
                DocKind.TITLE -> Unit // the screen's header already says "Privacy policy"
                DocKind.NOTE -> Text(b.text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(vertical = 4.dp))
                // Upright: label lettering is for names printed on a cartridge, not a document's headings (DESIGN.md).
                DocKind.HEADING -> Text(b.text, style = MaterialTheme.typography.bodyLarge, fontSize = 18.sp, fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(top = 20.dp, bottom = 6.dp).semantics { heading() })
                DocKind.SUBHEADING -> Text(b.text, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(top = 14.dp, bottom = 4.dp).semantics { heading() })
                DocKind.TEXT -> Text(bold(b.text), style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(vertical = 4.dp))
                DocKind.BULLET -> Row(Modifier.padding(vertical = 3.dp)) {
                    Text("•", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(Modifier.width(10.dp))
                    Text(bold(b.text), style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
                }
            }
        }
    }
}

@Composable
private fun assetText(path: String): String {
    val assets = LocalContext.current.assets
    return remember(path) { try { assets.open(path).bufferedReader().use { it.readText() } } catch (e: Exception) { "" } }
}

private fun bold(text: String): AnnotatedString = buildAnnotatedString {
    for ((part, strong) in boldRuns(text)) if (strong) withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { append(part) } else append(part)
}

enum class DocKind { TITLE, NOTE, HEADING, SUBHEADING, TEXT, BULLET }

data class DocBlock(val kind: DocKind, val text: String)

/**
 * The little Markdown PRIVACY.md uses, as blocks: `#`/`##`/`###` headings, `_a note_`, `- ` points (indented lines go
 * on), paragraphs (lines joined until a blank line). Bold (`**`) stays in the text, for [boldRuns].
 */
fun docBlocks(text: String): List<DocBlock> {
    val out = mutableListOf<DocBlock>()
    var kind: DocKind? = null
    val buf = StringBuilder()
    fun flush() {
        kind?.let { k ->
            val t = buf.toString().trim()
            out += if (k == DocKind.TEXT && t.length > 1 && t.startsWith("_") && t.endsWith("_")) DocBlock(DocKind.NOTE, t.trim('_'))
                else DocBlock(k, t)
        }
        kind = null
        buf.clear()
    }
    for (raw in text.lines()) {
        val line = raw.trimEnd()
        when {
            line.isBlank() -> flush()
            line.startsWith("### ") -> { flush(); out += DocBlock(DocKind.SUBHEADING, line.drop(4).trim()) }
            line.startsWith("## ") -> { flush(); out += DocBlock(DocKind.HEADING, line.drop(3).trim()) }
            line.startsWith("# ") -> { flush(); out += DocBlock(DocKind.TITLE, line.drop(2).trim()) }
            line.startsWith("- ") -> { flush(); kind = DocKind.BULLET; buf.append(line.drop(2).trim()) }
            else -> {
                if (kind == null) kind = DocKind.TEXT
                if (buf.isNotEmpty()) buf.append(' ')
                buf.append(line.trim())
            }
        }
    }
    flush()
    return out
}

/** A plain-text licence with its paragraphs' lines joined, so they flow to the screen's width. */
fun reflow(text: String): String =
    text.replace("\r", "").split(Regex("\n\\s*\n")).joinToString("\n\n") { p -> p.lines().joinToString(" ") { it.trim() }.trim() }.trim()

/** [text] split at `**` into runs, each with whether it is bold; the stars themselves are dropped. */
fun boldRuns(text: String): List<Pair<String, Boolean>> =
    text.split("**").mapIndexedNotNull { i, part -> if (part.isEmpty()) null else part to (i % 2 == 1) }
