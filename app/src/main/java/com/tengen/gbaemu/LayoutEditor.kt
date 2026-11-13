package com.tengen.gbaemu

import android.graphics.Bitmap
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import kotlin.math.roundToInt

/**
 * The touch layout editor (Settings → Touch buttons → Edit upright / sideways layout): the buttons and the game picture as in play
 * ([picture]: the paused game's, or a plain block), dragged with one finger; a tap picks one and the bar shows its
 * size slider (½×–2×). Reset all / Cancel / Save; Back = Cancel. The caller turns the screen to [sideways] first.
 */
@Composable
fun LayoutEditor(sideways: Boolean, start: Map<LayoutItem, Place>, picture: Bitmap?, look: Int,
                 onSave: (Map<LayoutItem, Place>) -> Unit, onCancel: () -> Unit, skin: Skin = Skins.PLAIN,
                 fitMode: Int = Settings.SIZE_FIT, console: Console = Console.GBA) {
    var edits by remember { mutableStateOf(start) }
    var selected by remember { mutableStateOf<LayoutItem?>(null) }
    BackHandler { onCancel() }
    val accent = LocalBrightAccent.current.toArgb()
    BoxWithConstraints(Modifier.fillMaxSize().background(Color(skin.body))) {
        val turnedRight = (maxWidth > maxHeight) == sideways // the screen has turned to the edited orientation
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { TouchControlsView(it).apply { editing = true } },
            update = { v ->
                v.sideways = sideways
                v.edits = edits
                v.look = look
                v.skin = skin
                v.fitMode = fitMode
                v.console = console
                v.picture = picture
                v.selected = selected
                v.onSelect = { selected = it }
                v.onEdit = { edits = it }
                v.selectColor = accent
            },
        )
        if (!turnedRight) return@BoxWithConstraints // a moment while the screen turns
        val layout = TouchLayout(constraints.maxWidth.toFloat(), constraints.maxHeight.toFloat(),
            LocalDensity.current.density, sideways, edits, console = console)
        // The bar away from the picked item (nothing picked: at the top, where upright the picture is big enough).
        val atTop = selected?.let { layout.box(it).cy > layout.height / 2 } ?: true
        Surface(
            color = MaterialTheme.colorScheme.surfaceContainerLow.copy(alpha = 0.94f), shape = MaterialTheme.shapes.medium,
            modifier = Modifier.align(if (atTop) Alignment.TopCenter else Alignment.BottomCenter)
                .systemBarsPadding().padding(8.dp).widthIn(max = 480.dp).fillMaxWidth(),
        ) {
            Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp)) {
                val item = selected
                if (item == null) {
                    Text("Drag anything to move it. Tap one to change its size.", fontSize = 13.sp)
                } else {
                    val scale = layout.place(item).scale
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("${item.label} · size ${(scale * 100).roundToInt()}%", fontSize = 13.sp, modifier = Modifier.weight(1f))
                    }
                    Slider(
                        value = scale, valueRange = TouchLayout.MIN_SCALE..TouchLayout.MAX_SCALE,
                        onValueChange = { s ->
                            val sized = layout.with(item, layout.place(item).copy(scale = s))
                            edits = edits + (item to sized.place(item)) // kept inside the screen
                        },
                    )
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TextButton(onClick = { edits = emptyMap(); selected = null }) { Text("Reset all") }
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = onCancel) { Text("Cancel") }
                    Button(onClick = { onSave(edits) }) { Text("Save") }
                }
                Text(if (sideways) "Sideways layout" else "Upright layout", fontSize = 11.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}
