package com.tengen.gbaemu

import android.graphics.BitmapFactory
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.EmojiEvents
import androidx.compose.material.icons.filled.Leaderboard
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onPlaced
import androidx.compose.ui.layout.positionInParent
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** A badge picture from [badges] (downloaded off the UI thread the first time); null while there is none. */
@Composable
fun rememberBadge(url: String, badges: BadgeCache): ImageBitmap? = produceState<ImageBitmap?>(null, url) {
    value = if (url.isEmpty()) null else withContext(Dispatchers.IO) {
        badges.bytes(url)?.let { BitmapFactory.decodeByteArray(it, 0, it.size)?.asImageBitmap() }
    }
}.value

/**
 * A badge: RetroAchievements' picture, printed like a small label on label stock (4 dp corners); a trophy on bare
 * plastic while there is none (not downloaded yet, or no internet).
 */
@Composable
fun Badge(url: String, badges: BadgeCache, size: Dp) {
    val picture = rememberBadge(url, badges)
    Box(Modifier.size(size).clip(RoundedCornerShape(4.dp))
        .background(if (picture != null) Color(Palette.LABEL_STOCK) else LocalPak.current.cartHigh),
        contentAlignment = Alignment.Center) {
        if (picture != null) Image(picture, contentDescription = null, modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
        else Icon(Icons.Filled.EmojiEvents, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.size(size * 0.55f))
    }
}

/**
 * RetroAchievements over the game (14), at the top in the middle (below the speed label when it shows, clear of the
 * camera cutout): one card at a time — a little moulded cartridge that drops in from the top — and under it, on the
 * scrim like the speed label, the challenges going on and the latest progress. Nothing here takes touches, so the
 * touch buttons under it keep working.
 */
@Composable
fun BoxScope.RaPopupLayer(view: RaPopups.View, badges: BadgeCache, belowSpeedLabel: Boolean = false) {
    Column(Modifier.align(Alignment.TopCenter).windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top))
        .padding(top = if (belowSpeedLabel) 36.dp else 8.dp, start = 12.dp, end = 12.dp).widthIn(max = 400.dp),
        horizontalAlignment = Alignment.CenterHorizontally) {
        // The card keeps its last contents while it slides away.
        var shown by remember { mutableStateOf<RaPopups.Card?>(null) }
        view.card?.let { shown = it }
        AnimatedVisibility(view.card != null,
            enter = fadeIn(tween(160)) + slideInVertically(tween(220)) { -it / 2 },
            exit = fadeOut(tween(200)) + slideOutVertically(tween(200)) { -it / 3 }) {
            shown?.let { RaCard(it, badges) }
        }
        // Two pills: the challenges going on, and the progress (its badge and its count) apart from them.
        Row(Modifier.padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            if (view.challenges.isNotEmpty()) {
                Row(Modifier.background(Scrim, RoundedCornerShape(50)).padding(4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    view.challenges.forEach { Badge(it.badgeUrl, badges, 28.dp) }
                }
            }
            view.progress?.let { p ->
                Row(Modifier.background(Scrim, RoundedCornerShape(50)).padding(4.dp), verticalAlignment = Alignment.CenterVertically) {
                    Badge(p.badgeUrl, badges, 28.dp)
                    Text(p.text, color = Color.White, style = MaterialTheme.typography.labelMedium.merge(Tabular),
                        modifier = Modifier.padding(start = 6.dp, end = 8.dp))
                }
            }
        }
        // Leaderboard counters (15): one overlay pill each, the board mark and rcheevos' text (a time, a score) in white
        // tag lettering with tabular figures, so a running clock doesn't jitter. As tall as the badge pills.
        if (view.trackers.isNotEmpty()) {
            Row(Modifier.padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                view.trackers.forEach { t ->
                    Row(Modifier.heightIn(min = 36.dp).background(Scrim, RoundedCornerShape(50)).padding(start = 10.dp, end = 12.dp)
                        .clearAndSetSemantics { contentDescription = "Leaderboard ${t.text}" },
                        verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Filled.Leaderboard, contentDescription = null, tint = Color.White, modifier = Modifier.size(16.dp))
                        Text(t.text, color = Color.White, style = MaterialTheme.typography.labelMedium.merge(Tabular),
                            modifier = Modifier.padding(start = 6.dp))
                    }
                }
            }
        }
    }
}

/** A leaderboard's mark on its pop-up card, where an achievement has its badge: bare plastic with the board icon. */
@Composable
private fun BoardMark(size: Dp) {
    Box(Modifier.size(size).clip(RoundedCornerShape(4.dp)).background(LocalPak.current.cartHigh), contentAlignment = Alignment.Center) {
        Icon(Icons.Filled.Leaderboard, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.size(size * 0.55f))
    }
}

private fun isBoardCard(kind: Int) =
    kind == RaEvent.LB_STARTED || kind == RaEvent.LB_FAILED || kind == RaEvent.LB_SUBMITTED || kind == RaEvent.LB_SCOREBOARD

/**
 * One pop-up card: a moulded cartridge with a short lip and its grip ridges. An unlock: the badge as its label, the
 * achievement's name in label lettering, "Unlocked · " and its description, the points. "Mastered": the game's band
 * across the top. A problem: its title in label red (upright: a message, not a printed name). A note: one line.
 * Read out once by TalkBack as one sentence.
 */
@Composable
private fun RaCard(card: RaPopups.Card, badges: BadgeCache) {
    val note = card.kind == RaPopups.NOTE
    val spoken = when (card.kind) {
        RaEvent.UNLOCKED -> "Achievement unlocked: ${card.title}. ${card.text}. ${card.points} points"
        RaEvent.MASTERED -> "${card.title} mastered: every achievement unlocked. ${card.points} points"
        RaEvent.LB_SCOREBOARD -> "Leaderboard ${card.title}: rank ${card.rank}. ${card.text}"
        RaEvent.LB_STARTED, RaEvent.LB_FAILED, RaEvent.LB_SUBMITTED -> "${card.text}: ${card.title}"
        else -> listOf(card.title, card.text).filter { it.isNotEmpty() }.joinToString(". ")
    }
    Cartridge(Modifier.fillMaxWidth().clearAndSetSemantics { contentDescription = spoken; liveRegion = LiveRegionMode.Polite },
        lip = 6.dp, inset = 12.dp) {
        if (card.kind == RaEvent.MASTERED) LabelBand(card.title, Modifier.padding(bottom = 10.dp))
        Row(Modifier.padding(bottom = if (note) 4.dp else 6.dp), verticalAlignment = Alignment.CenterVertically) {
            if (card.badgeUrl.isNotEmpty()) {
                Badge(card.badgeUrl, badges, 52.dp)
                Spacer(Modifier.width(12.dp))
            } else if (isBoardCard(card.kind)) {
                BoardMark(if (card.kind == RaEvent.LB_SCOREBOARD) 52.dp else 40.dp)
                Spacer(Modifier.width(12.dp))
            }
            Column(Modifier.weight(1f)) {
                when (card.kind) {
                    RaEvent.MASTERED -> Text("Mastered: every achievement unlocked", style = MaterialTheme.typography.bodyMedium)
                    RaPopups.NOTE -> Text(card.title, style = MaterialTheme.typography.bodyMedium)
                    RaEvent.UNLOCKED -> {
                        Text(card.title, style = MaterialTheme.typography.titleSmall, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        Text(if (card.text.isEmpty()) "Unlocked" else "Unlocked · ${card.text}", style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 2, overflow = TextOverflow.Ellipsis)
                    }
                    // A board's name is printed like an achievement's; what happened goes under it.
                    RaEvent.LB_STARTED, RaEvent.LB_FAILED, RaEvent.LB_SUBMITTED, RaEvent.LB_SCOREBOARD -> {
                        Text(card.title, style = MaterialTheme.typography.titleSmall, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        // A score's line is three facts: "Score X" on its own line, then the players and the best (no
                        // separator left hanging at a line end); a note is one line.
                        val lines = if (card.kind == RaEvent.LB_SCOREBOARD) card.text.split(" · ", limit = 2) else listOf(card.text)
                        if (lines.size == 2) Text(keepTogether(lines[0]), style = MaterialTheme.typography.bodySmall.merge(Tabular),
                            color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        Text(keepTogether(lines.last()), style = MaterialTheme.typography.bodySmall.merge(Tabular),
                            color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 2, overflow = TextOverflow.Ellipsis)
                    }
                    else -> {
                        Text(card.title, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Medium,
                            color = if (card.kind == RaEvent.SERVER_ERROR || card.kind == RaEvent.LB_ERROR) MaterialTheme.colorScheme.error
                                else MaterialTheme.colorScheme.onSurface)
                        if (card.text.isNotEmpty()) Text(card.text, style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 2, overflow = TextOverflow.Ellipsis)
                    }
                }
            }
            if (card.points > 0) Points(card.points, unlocked = true)
            if (card.kind == RaEvent.LB_SCOREBOARD && card.rank > 0) RankStamp(card.rank)
        }
        // The grip: inside the body (a modifier on the cartridge itself would draw under its plastic).
        Spacer(Modifier.fillMaxWidth().height(14.dp).gripRidges(LocalPak.current.shade))
    }
}

/** A score's place, printed like the points: "#4" in label lettering with a small "rank" tag under it, in ink (not the accent: it can't be pressed). */
@Composable
private fun RankStamp(rank: Int) {
    Column(Modifier.padding(start = 10.dp).widthIn(min = 32.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Text("#" + String.format(java.util.Locale.US, "%,d", rank), style = MaterialTheme.typography.titleMedium.merge(Tabular),
            color = MaterialTheme.colorScheme.onSurface)
        Text("rank", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/** Points, printed in label lettering with a small "pts" tag under them. */
@Composable
private fun Points(points: Int, unlocked: Boolean) {
    val color = if (unlocked) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant
    Column(Modifier.padding(start = 10.dp).widthIn(min = 32.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Text("$points", style = MaterialTheme.typography.titleMedium.merge(Tabular), color = color)
        Text("pts", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/**
 * Pause menu → Achievements (14): how many are unlocked (a line and a bar), then every achievement, unlocked first;
 * locked ones are quieter (the grey badge, soft ink). [summary]: raSummary's numbers.
 */
@Composable
fun AchievementsPanel(game: String, summary: IntArray?, list: List<RaAchievement>, hardcore: Boolean, badges: BadgeCache,
                      onBack: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(bottom = 16.dp).widthIn(min = 300.dp)) {
            ScreenHeader("Achievements", onBack = onBack, subtitle = game)
            val (rows, warning) = RaAchievement.split(list)
            summary?.let { s ->
                Text("${s[0]} of ${s[1]} unlocked · ${s[2]} of ${s[3]} points" + if (hardcore) " · Hardcore" else "",
                    style = MaterialTheme.typography.bodyMedium.merge(Tabular), color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(top = 4.dp))
                Spacer(Modifier.height(8.dp))
                ShareBar(if (s[1] == 0) 0f else s[0].toFloat() / s[1])
                Spacer(Modifier.height(6.dp))
            }
            warning?.let { // the server's notice about this app, e.g. hardcore unlocks not counting yet
                Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(bottom = 6.dp))
            }
            // A list longer than its room is cut partway through a row, so it plainly goes on (never on a divider).
            BoxWithConstraints(Modifier.weight(1f, fill = false)) {
                val density = LocalDensity.current
                val placed = remember(list) { mutableStateMapOf<Int, IntRange>() } // each row's top..bottom in the list, px
                val room = minOf(maxHeight, 420.dp)
                val cut = with(density) { listCut(placed.values, room.toPx(), 24.dp.toPx()).toDp() }
                Column(Modifier.heightIn(max = cut).verticalScroll(rememberScrollState())) {
                    rows.forEachIndexed { i, a ->
                        Column(Modifier.onPlaced { c ->
                            val top = c.positionInParent().y.toInt()
                            placed[i] = top..(top + c.size.height)
                        }) {
                            AchievementRow(a, badges)
                            if (i < rows.lastIndex) HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                        }
                    }
                }
            }
        }
    }
}

/**
 * The list's height in [room] px: all of it when it fits; else [room], unless that cut falls within [margin] of a
 * row's edge — then the cut moves to just past the middle of that row (or of the row above), so a part row shows.
 */
internal fun listCut(rows: Collection<IntRange>, room: Float, margin: Float): Float {
    val sorted = rows.sortedBy { it.first }
    val total = sorted.lastOrNull()?.last ?: return room
    if (total <= room) return room
    val i = sorted.indexOfFirst { room >= it.first && room < it.last }
    if (i < 0) return room
    val r = sorted[i]
    if (room - r.first >= margin && r.last - room >= margin) return room
    val mid = r.first + (r.last - r.first) * 0.55f
    if (mid <= room) return mid
    val above = sorted.getOrNull(i - 1) ?: return room
    return above.first + (above.last - above.first) * 0.55f
}

/** How much of the set is unlocked: a thin ink bar on a recessed track (the accent is kept for what can be pressed). */
@Composable
private fun ShareBar(share: Float) {
    val pak = LocalPak.current
    val ink = MaterialTheme.colorScheme.onSurface
    Box(Modifier.fillMaxWidth().height(6.dp).drawBehind {
        val r = CornerRadius(size.height / 2)
        drawRoundRect(pak.shade, cornerRadius = r)
        if (share > 0f) drawRoundRect(ink, size = Size(size.width * share.coerceIn(0f, 1f), size.height), cornerRadius = r)
    })
}

@Composable
private fun AchievementRow(a: RaAchievement, badges: BadgeCache) {
    Row(Modifier.fillMaxWidth().padding(vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Badge(a.badgeUrl, badges, 44.dp)
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(a.title, style = MaterialTheme.typography.titleSmall,
                color = if (a.unlocked) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant)
            Text(a.description, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 3, overflow = TextOverflow.Ellipsis)
            if (!a.unlocked && a.progress.isNotEmpty()) {
                Text(a.progress, style = MaterialTheme.typography.labelMedium.merge(Tabular),
                    color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(top = 2.dp))
            }
        }
        Points(a.points, a.unlocked)
    }
}

/**
 * Pause menu → Leaderboards (15): the game's boards; an opened board shows its scores — the top 10, then (when the
 * user is further down) a gap and the rows around the user, the user's row on bare plastic. [scores]: null while no
 * board is open. Scores load in hardcore and softcore; only hardcore sends them, which the list says when it is off.
 */
@Composable
fun LeaderboardsPanel(game: String, boards: List<RaBoard>, hardcore: Boolean, open: RaBoard?, scores: BoardScores?,
                      onOpen: (RaBoard) -> Unit, onRetry: () -> Unit, onBack: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(bottom = 16.dp).widthIn(min = 300.dp)) {
            if (open == null) {
                ScreenHeader("Leaderboards", onBack = onBack, subtitle = game)
                if (!hardcore) Text("Scores are only sent in Hardcore mode", style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(top = 4.dp, bottom = 6.dp))
                CutList(boards.size, boards) { i ->
                    val b = boards[i]
                    Row(Modifier.fillMaxWidth().heightIn(min = 64.dp).clickable { onOpen(b) }.padding(vertical = 10.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(b.title, style = MaterialTheme.typography.titleSmall)
                            if (b.description.isNotEmpty()) Text(b.description, style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        }
                        Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null,
                            tint = MaterialTheme.colorScheme.outline)
                    }
                }
            } else {
                ScreenHeader(open.title, onBack = onBack, subtitle = open.description.ifEmpty { null })
                when (scores) {
                    null, BoardScores.Loading -> Box(Modifier.fillMaxWidth().padding(vertical = 32.dp), contentAlignment = Alignment.Center) {
                        CircularProgressIndicator(Modifier.size(32.dp), color = MaterialTheme.colorScheme.onSurfaceVariant, strokeWidth = 3.dp)
                    }
                    is BoardScores.Failed -> Column(Modifier.padding(top = 8.dp, bottom = 4.dp)) {
                        Text(scores.message, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.error)
                        Spacer(Modifier.height(14.dp))
                        OutlinedButton(onClick = onRetry, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("Try again") }
                    }
                    is BoardScores.Loaded -> ScoreTable(scores)
                }
            }
        }
    }
}

/** A board's scores: how many play it, then rank, name and score per row; a gap where places are missing. */
@Composable
private fun ColumnScope.ScoreTable(s: BoardScores.Loaded) {
    if (s.rows.isEmpty()) {
        Text("No scores yet", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(vertical = 16.dp))
        return
    }
    val players = if (s.total == 1) "1 player" else String.format(java.util.Locale.US, "%,d players", s.total)
    Text(keepTogether(players + if (!s.onBoard) " · You haven't set a score yet" else ""),
        style = MaterialTheme.typography.bodyMedium.merge(Tabular), color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(top = 4.dp, bottom = 6.dp))
    val pak = LocalPak.current
    CutList(s.rows.size, s.rows, divided = { i -> !s.rows[i].gapBefore }, focus = s.rows.indexOfFirst { it.you }.takeIf { it >= 0 }) { i ->
        val r = s.rows[i]
        Column {
            // The gap names the places it skips, as a quiet tag between hairlines (not an icon: nothing here can be pressed).
            if (r.gapBefore) {
                val gap = gapLabel(s.rows.getOrNull(i - 1)?.rank ?: 0, r.rank)
                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                Text(gap, style = MaterialTheme.typography.labelMedium.merge(Tabular), color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp).clearAndSetSemantics { contentDescription = "$gap not shown" },
                    textAlign = TextAlign.Center)
            }
            Row(Modifier.fillMaxWidth()
                .then(if (r.you) Modifier.background(pak.cartHigh, RoundedCornerShape(4.dp)) else Modifier)
                .heightIn(min = 44.dp).padding(horizontal = 8.dp, vertical = 8.dp)
                .clearAndSetSemantics { contentDescription = (if (r.you) "You: " else "") + "rank ${r.rank}, ${r.name}, ${r.score}" },
                verticalAlignment = Alignment.CenterVertically) {
                // The user's rank is printed on a little label-stock sticker (day ink, both themes): their place stands out
                // on smoked plastic too, without the accent (it can't be pressed). The sticker reaches 6 dp left into the
                // row's padding so its figures line up with the ranks above and below; a hairline edge by day, where
                // label stock and the band are close.
                Box(Modifier.widthIn(min = 64.dp).padding(end = 10.dp)) {
                    Text(String.format(java.util.Locale.US, "%,d", r.rank), style = MaterialTheme.typography.titleSmall.merge(Tabular),
                        color = if (r.you) Color(Palette.LIGHT.ink) else MaterialTheme.colorScheme.onSurface,
                        modifier = if (r.you) Modifier.offset(x = (-6).dp)
                            .then(if (pak.dark) Modifier else Modifier.border(1.dp, MaterialTheme.colorScheme.outlineVariant, RoundedCornerShape(3.dp)))
                            .background(Color(Palette.LABEL_STOCK), RoundedCornerShape(3.dp))
                            .padding(horizontal = 6.dp, vertical = 1.dp) else Modifier)
                }
                Text(r.name, style = MaterialTheme.typography.bodyMedium, fontWeight = if (r.you) FontWeight.Bold else null,
                    modifier = Modifier.weight(1f).padding(end = 12.dp), maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text(r.score, style = MaterialTheme.typography.bodyMedium.merge(Tabular))
            }
        }
    }
}

/**
 * A list in the pause panel (the Leaderboards page; the pause menu's rows sideways) that scrolls in at most 420 dp and is cut partway through a row when longer (listCut), so
 * it plainly goes on; rows divided by hairlines ([divided]: whether a divider goes above row i, i > 0).
 */
@Composable
internal fun ColumnScope.CutList(count: Int, key: Any, divided: (Int) -> Boolean = { true }, focus: Int? = null,
                                 row: @Composable (Int) -> Unit) {
    BoxWithConstraints(Modifier.weight(1f, fill = false)) {
        val density = LocalDensity.current
        val placed = remember(key) { mutableStateMapOf<Int, IntRange>() }
        val room = minOf(maxHeight, 420.dp)
        val cut = with(density) { listCut(placed.values, room.toPx(), 24.dp.toPx()).toDp() }
        val scroll = rememberScrollState()
        Column(Modifier.heightIn(max = cut).verticalScroll(scroll)) {
            for (i in 0 until count) {
                Column(Modifier.onPlaced { c ->
                    val top = c.positionInParent().y.toInt()
                    placed[i] = top..(top + c.size.height)
                }) {
                    if (i > 0 && divided(i)) HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    row(i)
                }
            }
        }
        // [focus] (the user's row) is brought into view once, with a bit of the next row under it so the list still
        // plainly goes on.
        val target = focus?.let { placed[it] }
        LaunchedEffect(key, target != null, cut) {
            if (target != null) {
                val view = with(density) { cut.toPx() }.toInt()
                scroll.scrollTo(scrollToShow(target, view, scroll.value, with(density) { 24.dp.toPx() }.toInt()))
            }
        }
    }
}

/** The scroll position (px) that shows [row] fully in a [view] px window now at [now], with [margin] px more under it. */
internal fun scrollToShow(row: IntRange, view: Int, now: Int, margin: Int): Int = when {
    row.last + margin > now + view -> row.last + margin - view
    row.first < now -> row.first
    else -> now
}
