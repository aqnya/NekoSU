package me.nekosu.aqnya.ui.component

import androidx.compose.foundation.clickable
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SegmentedListItem
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier

@Composable
fun AppListItem(
    title: String,
    modifier: Modifier = Modifier,
    summary: String? = null,
    leading: @Composable (() -> Unit)? = null,
    trailing: @Composable (() -> Unit)? = null,
    onClick: (() -> Unit)? = null,
) {
    SegmentedListItem(
        modifier = modifier.then(if (onClick != null) Modifier.clickable { onClick() } else Modifier),
        shapes = currentListItemShapes(),
        colors = ListItemDefaults.segmentedColors(containerColor = MaterialTheme.colorScheme.surfaceBright),
        leadingContent = leading,
        supportingContent = summary?.let { text -> { Text(text) } },
        trailingContent = trailing,
        content = { Text(title) },
    )
}
