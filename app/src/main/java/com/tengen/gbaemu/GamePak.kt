package com.tengen.gbaemu

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Outline
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.addOutline
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp

/*
 * The Game Pak pieces (12): a cartridge's moulded body, its label band and the slot it sits in.
 * Original shapes in the spirit of a handheld's cartridge — no logos, no seal.
 */

/**
 * A cartridge seen from the front: rounded corners, and along the top a raised lip a little narrower than the body
 * (the end that goes into the console). [lip]: the lip's height; the body's corners are [corner].
 */
class CartridgeShape(private val corner: Dp = 14.dp, private val lip: Dp = 7.dp) : Shape {
    override fun createOutline(size: Size, layoutDirection: LayoutDirection, density: Density): Outline {
        val c = with(density) { corner.toPx() }
        val l = with(density) { lip.toPx() }
        val inset = l * 2.2f // how much narrower the lip is on each side
        val lr = l * 0.8f // the lip's own corners
        val w = size.width
        val h = size.height
        val p = Path().apply {
            moveTo(inset + lr, 0f)
            lineTo(w - inset - lr, 0f)
            quadraticTo(w - inset, 0f, w - inset, lr)
            lineTo(w - inset, l)
            lineTo(w - c, l)
            quadraticTo(w, l, w, l + c)
            lineTo(w, h - c)
            quadraticTo(w, h, w - c, h)
            lineTo(c, h)
            quadraticTo(0f, h, 0f, h - c)
            lineTo(0f, l + c)
            quadraticTo(0f, l, c, l)
            lineTo(inset, l)
            lineTo(inset, lr)
            quadraticTo(inset, 0f, inset + lr, 0f)
            close()
        }
        return Outline.Generic(p)
    }
}

/** Moulded plastic: [body] in [shape], a 1 dp [highlight] along its top edges and a [shade] lip below it. */
fun Modifier.moulded(shape: Shape, body: Color, highlight: Color, shade: Color): Modifier = drawBehind {
    val path = Path().apply { addOutline(shape.createOutline(size, layoutDirection, this@drawBehind)) }
    translate(top = 1.5.dp.toPx()) { drawPath(path, shade) }
    clipPath(path) {
        drawRect(highlight)
        translate(top = 1.dp.toPx()) { drawPath(path, body) }
    }
}

/** The opposite: a recess — a dark rim along the top, a faint light line along the bottom. */
fun Modifier.recessed(shape: Shape, floor: Color, rim: Color, light: Color): Modifier = drawBehind {
    val path = Path().apply { addOutline(shape.createOutline(size, layoutDirection, this@drawBehind)) }
    clipPath(path) {
        drawRect(light)
        translate(top = -1.dp.toPx()) { drawPath(path, rim) }
        translate(top = 2.dp.toPx()) { drawPath(path, floor) }
        translate(top = 1.dp.toPx()) { drawPath(path, floor) }
    }
}

/** A cartridge's grip: [count] fine ridges across the bottom of the body. */
fun Modifier.gripRidges(color: Color, count: Int = 3): Modifier = drawBehind {
    val gap = 3.dp.toPx()
    val inset = 22.dp.toPx()
    for (i in 0 until count) {
        val y = size.height - 6.dp.toPx() - i * gap
        drawLine(color, Offset(inset, y), Offset(size.width - inset, y), strokeWidth = 1.dp.toPx())
    }
}

/**
 * A cartridge: the moulded body (pressable: tap [onClick], hold [onLongClick]) holding its [content] below the lip.
 * [lip] sets the lip's height; the body's padding is [inset] on the sides.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun Cartridge(modifier: Modifier = Modifier, onClick: (() -> Unit)? = null, onLongClick: (() -> Unit)? = null,
              lip: Dp = 7.dp, inset: Dp = 8.dp, body: Color = LocalPak.current.cart,
              content: @Composable ColumnScope.() -> Unit) {
    val pak = LocalPak.current
    val shape = CartridgeShape(lip = lip)
    var m = modifier.moulded(shape, body, pak.highlight, pak.shade).clip(shape)
    if (onClick != null) m = m.combinedClickable(onClick = onClick, onLongClick = onLongClick, role = Role.Button)
    CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.onSurface) {
        Column(m.padding(start = inset, end = inset, top = lip + inset * 0.75f), content = content)
    }
}

/**
 * A label band: the black strip across the top of a cartridge's label with the game's name in white label lettering.
 * [big]: the seated cartridge's larger lettering. [trailing]: e.g. the ⋮ button, white on the band.
 */
@Composable
fun LabelBand(name: String, modifier: Modifier = Modifier, big: Boolean = false, lines: Int = if (big) 1 else 2,
              trailing: @Composable RowScope.() -> Unit = {}) {
    val pak = LocalPak.current
    Row(modifier.fillMaxWidth().clip(RoundedCornerShape(topStart = 4.dp, topEnd = 4.dp)).background(pak.band)
        .heightIn(min = if (big) 48.dp else 40.dp).padding(start = 10.dp, end = 2.dp, top = 6.dp, bottom = 6.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Text(name, color = Color.White, maxLines = lines, overflow = TextOverflow.Ellipsis,
            style = if (big) MaterialTheme.typography.titleMedium else MaterialTheme.typography.titleSmall,
            modifier = Modifier.weight(1f).padding(end = 8.dp))
        CompositionLocalProvider(LocalContentColor provides Color.White.copy(alpha = 0.85f)) { trailing() }
    }
}

/** The slot: a dark recessed bay, its mouth (a darker strip with a lit edge) across the top, then [content]. */
@Composable
fun SlotBay(modifier: Modifier = Modifier, content: @Composable BoxScope.() -> Unit) {
    val pak = LocalPak.current
    val shape = RoundedCornerShape(20.dp)
    Box(modifier.fillMaxWidth().recessed(shape, pak.slot, Color.Black, pak.highlight.copy(alpha = 0.5f)).clip(shape)) {
        Box(Modifier.fillMaxWidth().padding(horizontal = 18.dp).padding(top = 10.dp).drawBehind {
            val h = 10.dp.toPx()
            drawRoundRect(pak.slotMouth, size = Size(size.width, h), cornerRadius = CornerRadius(h / 2))
            drawLine(Color.White.copy(alpha = 0.10f), Offset(h / 2, h), Offset(size.width - h / 2, h), strokeWidth = 1.dp.toPx())
        })
        content()
    }
}

/** An empty slot: a dashed cartridge outline with [text] in it. */
@Composable
fun EmptyCartOutline(text: String, modifier: Modifier = Modifier) {
    val color = Color.White.copy(alpha = 0.32f)
    val shape = CartridgeShape(lip = 9.dp)
    Box(modifier.drawBehind {
        val path = Path().apply { addOutline(shape.createOutline(size, layoutDirection, this@drawBehind)) }
        drawPath(path, color, style = Stroke(width = 1.5.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(14f, 10f))))
    }.padding(24.dp), contentAlignment = Alignment.Center) {
        Text(text, color = Color.White.copy(alpha = 0.78f), style = MaterialTheme.typography.bodyMedium)
    }
}
