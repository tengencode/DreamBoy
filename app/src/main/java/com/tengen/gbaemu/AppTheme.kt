package com.tengen.gbaemu

import android.app.Activity
import android.os.Build
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.Shapes
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat

/**
 * The app's look (milestone 12, "Game Pak"): screens of moulded cartridge plastic — light grey by day, smoked black at
 * night, following the phone — games as cartridges with their picture as the label, the slot dark in both. One
 * see-through-plastic accent from the console skin marks only what can be pressed. Screens read these roles.
 */
@Immutable
class PakColors(
    val cart: Color, val cartHigh: Color, val highlight: Color, val shade: Color,
    val slot: Color, val slotMouth: Color, val band: Color, val bandEdge: Color, val dark: Boolean,
)

val LocalPak = compositionLocalOf { pakColors(Palette.DARK, dark = true) }

/** The accent's bright form (the dark theme's), for marks drawn over the game or a skin, whatever the theme. */
val LocalBrightAccent = compositionLocalOf { Color(AccentColors.dark(AccentColors.DEFAULT_SEED).color) }

private fun pakColors(p: Palette.Roles, dark: Boolean) = PakColors(
    Color(p.cart), Color(p.cartHigh), Color(p.highlight), Color(p.shade),
    Color(p.slot), Color(p.slotMouth), Color(p.band), Color(p.bandEdge), dark,
)

private fun scheme(dark: Boolean, accent: AccentColors.Accent): ColorScheme {
    val p = if (dark) Palette.DARK else Palette.LIGHT
    val a = Color(accent.color)
    val onA = Color(accent.on)
    val base = if (dark) darkColorScheme() else lightColorScheme()
    return base.copy(
        primary = a, onPrimary = onA, primaryContainer = a.copy(alpha = 0.18f).compositeOn(Color(p.cart)), onPrimaryContainer = Color(p.ink),
        secondary = a, onSecondary = onA, secondaryContainer = Color(p.cartHigh), onSecondaryContainer = Color(p.ink),
        tertiary = a, onTertiary = onA,
        background = Color(p.plastic), onBackground = Color(p.ink),
        surface = Color(p.plastic), onSurface = Color(p.ink),
        surfaceVariant = Color(p.cartHigh), onSurfaceVariant = Color(p.inkSoft),
        surfaceContainerLowest = Color(p.plastic), surfaceContainerLow = Color(p.cart),
        surfaceContainer = Color(p.cart), surfaceContainerHigh = Color(p.cart), surfaceContainerHighest = Color(p.cartHigh),
        surfaceBright = Color(p.cartHigh), surfaceDim = Color(p.plastic), surfaceTint = Color.Transparent,
        inverseSurface = Color(p.ink), inverseOnSurface = Color(p.plastic), inversePrimary = a,
        error = Color(p.error), onError = Color(p.onError), errorContainer = Color(p.errorSoft), onErrorContainer = Color(p.error),
        outline = Color(p.outline), outlineVariant = Color(p.outline).copy(alpha = 0.45f).compositeOn(Color(p.cart)),
        scrim = Color.Black,
    )
}

private fun Color.compositeOn(under: Color): Color {
    val a = alpha
    return Color(red * a + under.red * (1 - a), green * a + under.green * (1 - a), blue * a + under.blue * (1 - a))
}

/** Roboto Black Italic: the lettering on cartridge labels — screen titles, names on bands, slot numbers. */
val LabelFace = FontFamily.SansSerif
/** Roboto Condensed Bold: small tags. */
val TagFace = FontFamily(android.graphics.Typeface.create("sans-serif-condensed", android.graphics.Typeface.BOLD))

private fun label(size: Int, line: Int, spacing: Float = 0f) = TextStyle(fontFamily = LabelFace, fontWeight = FontWeight.Black,
    fontStyle = FontStyle.Italic, fontSize = size.sp, lineHeight = line.sp, letterSpacing = spacing.sp)

val AppTypography: Typography = Typography().let { t ->
    t.copy(
        displayLarge = label(84, 80, -1f),
        displaySmall = label(40, 44, -0.5f),
        headlineMedium = label(30, 36, -0.3f),
        headlineSmall = label(26, 32, -0.2f),
        titleLarge = label(22, 28),
        titleMedium = label(18, 24),
        titleSmall = label(15, 19),
        labelLarge = t.labelLarge.copy(fontWeight = FontWeight.Bold),
        labelMedium = TextStyle(fontFamily = TagFace, fontWeight = FontWeight.Bold, fontSize = 13.sp, lineHeight = 16.sp,
            letterSpacing = 0.2.sp),
    )
}

val AppShapes = Shapes(
    extraSmall = RoundedCornerShape(6.dp), small = RoundedCornerShape(10.dp), medium = RoundedCornerShape(14.dp),
    large = RoundedCornerShape(18.dp), extraLarge = RoundedCornerShape(22.dp),
)

/** The dimmed layer behind menus over the game. */
val Scrim = Color(0xB0000000)

/**
 * The theme: [dark] follows the phone; [accentSeed] is the chosen skin's plastic ([AccentColors.seedFor]). Also sets
 * the status and navigation bars' colours and icons to match (the activity isn't recreated when dark mode changes).
 */
@Composable
fun AppTheme(dark: Boolean = isSystemInDarkTheme(), accentSeed: Int = AccentColors.DEFAULT_SEED, content: @Composable () -> Unit) {
    val accent = remember(dark, accentSeed) { if (dark) AccentColors.dark(accentSeed) else AccentColors.light(accentSeed) }
    val colors = remember(dark, accent) { scheme(dark, accent) }
    val pak = remember(dark) { pakColors(if (dark) Palette.DARK else Palette.LIGHT, dark) }
    val view = LocalView.current
    if (!view.isInEditMode) SideEffect {
        val window = (view.context as? Activity)?.window ?: return@SideEffect
        val plastic = colors.background.toArgb()
        @Suppress("DEPRECATION")
        if (Build.VERSION.SDK_INT < 35) {
            window.statusBarColor = plastic
            window.navigationBarColor = plastic
        }
        WindowCompat.getInsetsController(window, view).apply {
            isAppearanceLightStatusBars = !dark
            isAppearanceLightNavigationBars = !dark
        }
    }
    val bright = remember(accentSeed) { Color(AccentColors.dark(accentSeed).color) }
    androidx.compose.runtime.CompositionLocalProvider(LocalPak provides pak, LocalBrightAccent provides bright) {
        MaterialTheme(colorScheme = colors, typography = AppTypography, shapes = AppShapes, content = content)
    }
}

/**
 * A screen's top bar: a back arrow (if [onBack]), the title in label lettering (and a smaller line under it), icon
 * buttons on the right.
 */
@Composable
fun ScreenHeader(title: String, onBack: (() -> Unit)? = null, subtitle: String? = null, actions: @Composable RowScope.() -> Unit = {}) {
    Row(Modifier.fillMaxWidth().heightIn(min = 64.dp).padding(start = if (onBack == null) 20.dp else 4.dp, end = 4.dp, top = 4.dp),
        verticalAlignment = Alignment.CenterVertically) {
        if (onBack != null) {
            IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") }
        }
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.headlineSmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (subtitle != null) {
                Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
        actions()
    }
}

/** A flat row of the pause menu: an icon and its label (no card inside the panel — the menu stays sparse). Off: faded,
 * with [note] saying why. */
@Composable
fun MenuRow(icon: ImageVector, label: String, onClick: () -> Unit, enabled: Boolean = true, note: String? = null) {
    val fade = Modifier.alpha(if (enabled) 1f else 0.38f)
    Row(Modifier.fillMaxWidth().clickable(enabled = enabled, onClick = onClick).heightIn(min = 52.dp).padding(horizontal = 4.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.size(24.dp).then(fade))
        Spacer(Modifier.width(16.dp))
        Column {
            Text(label, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Medium, modifier = fade)
            if (note != null) Text(note, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

/** 2-4 short options side by side; tapping one picks it. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun Choices(options: List<String>, selected: Int, onPick: (Int) -> Unit, modifier: Modifier = Modifier) {
    SingleChoiceSegmentedButtonRow(modifier.fillMaxWidth()) {
        options.forEachIndexed { i, label ->
            SegmentedButton(
                selected = i == selected, onClick = { onPick(i) },
                shape = SegmentedButtonDefaults.itemShape(index = i, count = options.size),
                icon = {}, // no tick: the colour shows the choice, and the labels keep their room
                colors = SegmentedButtonDefaults.colors(activeContainerColor = MaterialTheme.colorScheme.primary,
                    activeContentColor = MaterialTheme.colorScheme.onPrimary, inactiveContainerColor = MaterialTheme.colorScheme.background,
                    inactiveContentColor = MaterialTheme.colorScheme.onSurface,
                    activeBorderColor = MaterialTheme.colorScheme.primary, inactiveBorderColor = MaterialTheme.colorScheme.outline),
            ) { Text(label, maxLines = 1, fontSize = 13.sp, fontWeight = FontWeight.Medium) }
        }
    }
}

/** A setting's help: under the setting it explains. */
@Composable
fun Help(text: String) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(top = 6.dp))
}

/** An on/off setting: the whole row toggles it. */
@Composable
fun SwitchRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit, help: String? = null) {
    Column(Modifier.fillMaxWidth().clickable { onChange(!checked) }.padding(horizontal = 16.dp, vertical = 14.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(label, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
            Switch(checked = checked, onCheckedChange = onChange)
        }
        if (help != null) Help(help)
    }
}

/** A setting with 2-4 options shown at once: its label, the [Choices], its help. */
@Composable
fun ChoiceRow(label: String, options: List<String>, selected: Int, onPick: (Int) -> Unit, help: String? = null) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp)) {
        Text(label, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.padding(bottom = 10.dp))
        Choices(options, selected, onPick)
        if (help != null) Help(help)
    }
}

/**
 * A row that opens a page: an icon, its title, what it is set to now, and a chevron. [enabled] false: greyed out, a
 * tap does nothing. Inside a [Group] the rows are divided by hairlines; [last] leaves the line off.
 */
@Composable
fun NavRow(icon: ImageVector?, title: String, summary: String?, enabled: Boolean = true, last: Boolean = false, onClick: () -> Unit) {
    // Off: the icon, title and chevron fade; the summary (often the reason it's off) stays readable.
    val fade = Modifier.alpha(if (enabled) 1f else 0.38f)
    Row(Modifier.fillMaxWidth().clickable(enabled = enabled, onClick = onClick)
        .heightIn(min = 64.dp).padding(horizontal = 16.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically) {
        if (icon != null) {
            Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant, modifier = fade.size(24.dp))
            Spacer(Modifier.width(16.dp))
        }
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Medium, modifier = fade)
            if (!summary.isNullOrEmpty()) {
                Text(summary, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null, tint = MaterialTheme.colorScheme.outline,
            modifier = fade)
    }
    if (!last) HorizontalDivider(Modifier.padding(start = if (icon != null) 56.dp else 16.dp),
        color = MaterialTheme.colorScheme.outlineVariant)
}

/** Settings rows grouped on a moulded plastic card. */
@Composable
fun Group(modifier: Modifier = Modifier, body: Color = LocalPak.current.cart, content: @Composable ColumnScope.() -> Unit) {
    val pak = LocalPak.current
    val shape = MaterialTheme.shapes.large
    Column(modifier.fillMaxWidth().padding(vertical = 6.dp).moulded(shape, body, pak.highlight, pak.shade)
        .clip(shape), content = content)
}

/** One line of an [ActionSheet]. [danger]: red, and set apart from the others. */
class SheetItem(val icon: ImageVector, val label: String, val danger: Boolean = false, val onClick: () -> Unit)

/** A panel from the bottom: a label band with [title], then icon + label lines; the danger lines come last, after a divider. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ActionSheet(title: String, items: List<SheetItem>, onClose: () -> Unit) {
    ModalBottomSheet(onDismissRequest = onClose, sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
        containerColor = LocalPak.current.cart) {
        Column(Modifier.navigationBarsPadding().padding(bottom = 12.dp)) {
            LabelBand(title, Modifier.padding(horizontal = 16.dp).padding(bottom = 8.dp), big = true)
            val (safe, danger) = items.partition { !it.danger }
            for (item in safe) SheetLine(item)
            if (danger.isNotEmpty()) {
                HorizontalDivider(Modifier.padding(vertical = 4.dp, horizontal = 24.dp), color = MaterialTheme.colorScheme.outlineVariant)
                for (item in danger) SheetLine(item)
            }
        }
    }
}

@Composable
private fun SheetLine(item: SheetItem) {
    val color = if (item.danger) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface
    Row(Modifier.fillMaxWidth().clickable(onClick = item.onClick).heightIn(min = 52.dp).padding(horizontal = 24.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Icon(item.icon, contentDescription = null, tint = if (item.danger) color else MaterialTheme.colorScheme.onSurfaceVariant)
        Spacer(Modifier.width(16.dp))
        Text(item.label, color = color, style = MaterialTheme.typography.bodyLarge)
    }
}

/** A chosen tile's ring (the Skin page). */
@Composable
fun chosenBorder(chosen: Boolean): BorderStroke? = if (chosen) BorderStroke(3.dp, MaterialTheme.colorScheme.primary) else null
