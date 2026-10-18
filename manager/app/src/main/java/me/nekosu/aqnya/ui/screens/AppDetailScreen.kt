package me.nekosu.aqnya.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.rounded.Badge
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Layers
import androidx.compose.material.icons.rounded.Lock
import androidx.compose.material.icons.rounded.LockOpen
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Shield
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import kotlinx.coroutines.launch
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ui.component.AppCard
import me.nekosu.aqnya.ui.component.AppCheckbox
import me.nekosu.aqnya.ui.component.AppDialog
import me.nekosu.aqnya.ui.component.AppListItem
import me.nekosu.aqnya.ui.component.AppRadioButton
import me.nekosu.aqnya.ui.component.AppSwitch
import me.nekosu.aqnya.ui.component.AppTextField
import me.nekosu.aqnya.ui.component.AppTopAppBar
import me.nekosu.aqnya.ui.component.CardGroup
import me.nekosu.aqnya.ui.theme.ContinuousCornerShape

// ─────────────────────────────────────────────────────────────────────────────
// Dialogs
// ─────────────────────────────────────────────────────────────────────────────

/** Build an [AppConfig] from the screen's draft values (used for live saving). */
private fun buildConfig(
    allowed: Boolean,
    rootUid: String,
    caps: Set<LinuxCap>,
    domain: String,
    ns: NksuNamespace,
): AppConfig =
    AppConfig(
        allowed = allowed,
        rootUid = rootUid.toIntOrNull() ?: 0,
        caps = caps,
        selinuxDomain = domain,
        namespace = ns,
    )

/**
 * Capabilities picker.
 *
 * A Material 3 Expressive [ModalBottomSheet]: a large title with the running
 * count, a search field, connected preset buttons and checkbox rows that tint
 * with the secondary container when selected.  Edits are applied live through
 * [onSelectionChange].
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun CapsSheet(
    current: Set<LinuxCap>,
    onDismiss: () -> Unit,
    onSelectionChange: (Set<LinuxCap>) -> Unit,
) {
    val haptic = LocalHapticFeedback.current
    val sheetState =
        rememberBottomSheetState(
            initialValue = SheetValue.Hidden,
            enabledValues = setOf(SheetValue.Hidden, SheetValue.Expanded),
        )
    val scope = rememberCoroutineScope()
    var query by rememberSaveable { mutableStateOf("") }

    val filtered =
        remember(query) {
            val q = query.trim()
            if (q.isEmpty()) {
                LinuxCap.entries
            } else {
                LinuxCap.entries.filter { cap ->
                    cap.label.contains(q, ignoreCase = true) ||
                        "CAP_${cap.label}".contains(q, ignoreCase = true) ||
                        cap.value.toString() == q
                }
            }
        }

    fun dismiss() {
        scope.launch { sheetState.hide() }.invokeOnCompletion { onDismiss() }
    }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = MaterialTheme.colorScheme.surfaceContainerLow,
    ) {
        Column(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 24.dp)
                    .padding(bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            // ── 标题 + 计数 ─────────────────────────────────────────────
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = stringResource(R.string.app_capabilities),
                        style = MaterialTheme.typography.headlineSmall,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Text(
                        text = stringResource(R.string.app_caps_selected, current.size, LinuxCap.entries.size),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                IconButton(
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                        dismiss()
                    },
                ) {
                    Icon(Icons.Rounded.Close, contentDescription = null)
                }
            }

            // ── 搜索 ───────────────────────────────────────────────────
            AppTextField(
                value = query,
                onValueChange = { query = it },
                label = stringResource(R.string.caps_search_hint),
                leadingIcon = {
                    Icon(Icons.Rounded.Search, contentDescription = null, modifier = Modifier.size(20.dp))
                },
                trailingIcon = {
                    if (query.isNotEmpty()) {
                        IconButton(onClick = { query = "" }) {
                            Icon(Icons.Rounded.Close, contentDescription = null, modifier = Modifier.size(18.dp))
                        }
                    }
                },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )

            // ── 预设 ───────────────────────────────────────────────────
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                CapsPresetButton(
                    text = stringResource(R.string.dialog_select_all),
                    modifier = Modifier.weight(1f),
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                        onSelectionChange(LinuxCap.entries.toSet())
                    },
                )
                CapsPresetButton(
                    text = stringResource(R.string.dialog_default),
                    modifier = Modifier.weight(1f),
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                        onSelectionChange(DEFAULT_CAPS)
                    },
                )
                CapsPresetButton(
                    text = stringResource(R.string.dialog_clear),
                    modifier = Modifier.weight(1f),
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                        onSelectionChange(emptySet())
                    },
                )
            }

            // ── 列表 ───────────────────────────────────────────────────
            LazyColumn(
                modifier = Modifier.fillMaxWidth().weight(1f, fill = false),
                verticalArrangement = Arrangement.spacedBy(3.dp),
            ) {
                if (filtered.isEmpty()) {
                    item {
                        Text(
                            text = stringResource(R.string.caps_empty),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            textAlign = TextAlign.Center,
                            modifier = Modifier.fillMaxWidth().padding(vertical = 24.dp),
                        )
                    }
                }
                items(filtered, key = { it.value }) { cap ->
                    CapRow(
                        cap = cap,
                        checked = current.contains(cap),
                        onToggle = {
                            onSelectionChange(if (current.contains(cap)) current - cap else current + cap)
                        },
                    )
                }
            }
        }
    }
}

@Composable
private fun CapsPresetButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    FilledTonalButton(
        onClick = onClick,
        modifier = modifier.defaultMinSize(minHeight = 40.dp),
        shape = ContinuousCornerShape(12.dp),
        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 6.dp),
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelLarge,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun CapRow(
    cap: LinuxCap,
    checked: Boolean,
    onToggle: () -> Unit,
) {
    val haptic = LocalHapticFeedback.current
    val container by
        animateColorAsState(
            targetValue = if (checked) MaterialTheme.colorScheme.secondaryContainer else Color.Transparent,
            label = "cap_container",
        )
    val contentColor =
        if (checked) {
            MaterialTheme.colorScheme.onSecondaryContainer
        } else {
            MaterialTheme.colorScheme.onSurface
        }

    Row(
        modifier =
            Modifier
                .fillMaxWidth()
                .clip(ContinuousCornerShape(16.dp))
                .background(container)
                .clickable(
                    interactionSource = remember { MutableInteractionSource() },
                    indication = null,
                ) {
                    haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                    onToggle()
                }.padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        AppCheckbox(checked = checked, onCheckedChange = null)
        Spacer(Modifier.width(10.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "CAP_${cap.label}",
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                fontFamily = FontFamily.Monospace,
                color = contentColor,
            )
            Text(
                text = stringResource(cap.descriptionRes),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
        }
        Spacer(Modifier.width(8.dp))
        Text(
            text = cap.value.toString(),
            style = MaterialTheme.typography.labelSmall,
            fontFamily = FontFamily.Monospace,
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f),
        )
    }
}

@Composable
private fun UidDialog(
    current: String,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit,
) {
    val haptic = LocalHapticFeedback.current
    var draft by remember { mutableStateOf(current) }

    AppDialog(
        onDismissRequest = onDismiss,
        title = stringResource(R.string.app_root_uid),
        confirmText = stringResource(R.string.dialog_confirm),
        onConfirm = {
            haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
            onConfirm(draft)
        },
        dismissText = stringResource(R.string.dialog_cancel),
    ) {
        Column {
            AppTextField(
                value = draft,
                onValueChange = { value ->
                    if (value.length <= 10 && value.all { it.isDigit() }) draft = value
                },
                label = stringResource(R.string.app_root_uid),
                leadingIcon = {
                    Icon(Icons.Rounded.Badge, contentDescription = null, modifier = Modifier.size(20.dp))
                },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            )
            Spacer(Modifier.height(8.dp))
            Text(
                text = stringResource(R.string.app_root_uid_desc),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun DomainDialog(
    current: String,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit,
) {
    val haptic = LocalHapticFeedback.current
    var draft by remember { mutableStateOf(current) }

    AppDialog(
        onDismissRequest = onDismiss,
        title = stringResource(R.string.app_selinux_domain),
        confirmText = stringResource(R.string.dialog_confirm),
        onConfirm = {
            haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
            onConfirm(draft.trim())
        },
        dismissText = stringResource(R.string.dialog_cancel),
    ) {
        Column {
            AppTextField(
                value = draft,
                onValueChange = { draft = it },
                label = stringResource(R.string.app_selinux_domain),
                leadingIcon = {
                    Icon(Icons.Rounded.Shield, contentDescription = null, modifier = Modifier.size(20.dp))
                },
                trailingIcon = {
                    if (draft != "u:r:nksu:s0") {
                        IconButton(onClick = { draft = "u:r:nksu:s0" }) {
                            Icon(
                                Icons.Rounded.Refresh,
                                contentDescription = stringResource(R.string.cd_reset),
                                modifier = Modifier.size(18.dp),
                            )
                        }
                    }
                },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Ascii),
            )
        }
    }
}

@Composable
private fun NamespaceDialog(
    current: NksuNamespace,
    onDismiss: () -> Unit,
    onConfirm: (NksuNamespace) -> Unit,
) {
    val haptic = LocalHapticFeedback.current
    var draft by remember { mutableStateOf(current) }

    AppDialog(
        onDismissRequest = onDismiss,
        title = stringResource(R.string.app_mount_namespace),
        confirmText = stringResource(R.string.dialog_confirm),
        onConfirm = {
            haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
            onConfirm(draft)
        },
        dismissText = stringResource(R.string.dialog_cancel),
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
            NksuNamespace.entries.forEach { option ->
                Row(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .clip(ContinuousCornerShape(12.dp))
                            .clickable(
                                interactionSource = remember { MutableInteractionSource() },
                                indication = null,
                            ) {
                                haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                draft = option
                            }.padding(horizontal = 4.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    AppRadioButton(selected = draft == option, onClick = null)
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = stringResource(option.labelRes),
                            style = MaterialTheme.typography.bodyMedium,
                            fontWeight = FontWeight.SemiBold,
                        )
                        Text(
                            text = stringResource(option.descriptionRes),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen
// ─────────────────────────────────────────────────────────────────────────────

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppDetailScreen(
    app: AppInfo,
    config: AppConfig?,
    onSave: (AppConfig) -> Unit,
    onBack: () -> Unit,
    navController: NavController,
) {
    var allowed by rememberSaveable { mutableStateOf(config?.allowed ?: false) }

    val capsSaver =
        remember {
            val labelMap = LinuxCap.entries.associateBy { it.label }
            Saver<Set<LinuxCap>, List<String>>(
                save = { it.map { cap -> cap.label } },
                restore = { it.mapNotNull { label -> labelMap[label] }.toSet() },
            )
        }
    var caps by rememberSaveable(stateSaver = capsSaver) { mutableStateOf(config?.caps ?: DEFAULT_CAPS) }

    var domain by rememberSaveable { mutableStateOf(config?.selinuxDomain ?: "u:r:nksu:s0") }

    // root 进程使用的 UID：0 表示真正的 root，否则提权后切换到该 UID。
    var rootUidText by rememberSaveable { mutableStateOf((config?.rootUid ?: 0).toString()) }

    val nsSaver =
        remember {
            val valueMap = NksuNamespace.entries.associateBy { it.value }
            Saver<NksuNamespace, Int>(
                save = { it.value },
                restore = { valueMap[it] ?: NksuNamespace.INHERITED },
            )
        }
    var ns by rememberSaveable(stateSaver = nsSaver) {
        mutableStateOf(config?.namespace ?: NksuNamespace.INHERITED)
    }

    var showCapsSheet by rememberSaveable { mutableStateOf(false) }
    var showUidDialog by rememberSaveable { mutableStateOf(false) }
    var showDomainDialog by rememberSaveable { mutableStateOf(false) }
    var showNsDialog by rememberSaveable { mutableStateOf(false) }
    val haptic = LocalHapticFeedback.current

    // 实时保存：任何一处改动都会立即下发，不再需要保存按钮。
    fun save(
        allowedValue: Boolean = allowed,
        rootUidValue: String = rootUidText,
        capsValue: Set<LinuxCap> = caps,
        domainValue: String = domain,
        nsValue: NksuNamespace = ns,
    ) = onSave(buildConfig(allowedValue, rootUidValue, capsValue, domainValue, nsValue))

    if (showCapsSheet) {
        CapsSheet(
            current = caps,
            onDismiss = { showCapsSheet = false },
            onSelectionChange = { selected ->
                caps = selected
                save(capsValue = selected)
            },
        )
    }
    if (showUidDialog) {
        UidDialog(
            current = rootUidText,
            onDismiss = { showUidDialog = false },
            onConfirm = { value ->
                val resolved = value.ifBlank { "0" }
                rootUidText = resolved
                showUidDialog = false
                save(rootUidValue = resolved)
            },
        )
    }
    if (showDomainDialog) {
        DomainDialog(
            current = domain,
            onDismiss = { showDomainDialog = false },
            onConfirm = { value ->
                val resolved = value.ifBlank { "u:r:nksu:s0" }
                domain = resolved
                showDomainDialog = false
                save(domainValue = resolved)
            },
        )
    }
    if (showNsDialog) {
        NamespaceDialog(
            current = ns,
            onDismiss = { showNsDialog = false },
            onConfirm = { value ->
                ns = value
                showNsDialog = false
                save(nsValue = value)
            },
        )
    }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.surfaceContainer,
        contentWindowInsets = WindowInsets(0.dp),
        topBar = {
            AppTopAppBar(
                title = stringResource(R.string.app_profile_title),
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            Icons.AutoMirrored.Rounded.ArrowBack,
                            contentDescription = stringResource(R.string.cd_back),
                        )
                    }
                },
            )
        },
    ) { innerPadding ->
        LazyColumn(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(innerPadding)
                    .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
            contentPadding = PaddingValues(top = 8.dp, bottom = 24.dp),
        ) {
            // ── Hero：应用身份 ────────────────────────────────────────────────
            item {
                Column(
                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                ) {
                    AppIcon(
                        packageName = app.packageName,
                        modifier =
                            Modifier
                                .size(72.dp)
                                .clip(ContinuousCornerShape(20.dp)),
                    )
                    Spacer(Modifier.height(16.dp))
                    Text(
                        text = app.name,
                        style = MaterialTheme.typography.headlineSmall,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Spacer(Modifier.height(4.dp))
                    Text(
                        text = app.packageName,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Spacer(Modifier.height(14.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        AppTag(
                            label = stringResource(R.string.app_uid_tag, app.uid),
                            color = MaterialTheme.colorScheme.tertiary,
                        )
                        if (allowed) {
                            AppTag(
                                label = stringResource(R.string.app_status_granted),
                                color = MaterialTheme.colorScheme.primary,
                            )
                        }
                        if (app.isSystem) {
                            AppTag(
                                label = stringResource(R.string.app_tag_system),
                                color = MaterialTheme.colorScheme.secondary,
                            )
                        }
                    }
                }
            }

            // ── Root 开关（重点卡片）────────────────────────────────────────
            item {
                RootAccessCard(
                    allowed = allowed,
                    onToggle = { value ->
                        haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                        val newCaps =
                            if (!value) {
                                emptySet()
                            } else if (caps.isEmpty()) {
                                DEFAULT_CAPS
                            } else {
                                caps
                            }
                        val newDomain = if (!value) "u:r:nksu:s0" else domain
                        val newNs = if (!value) NksuNamespace.INHERITED else ns
                        allowed = value
                        caps = newCaps
                        domain = newDomain
                        ns = newNs
                        save(
                            allowedValue = value,
                            capsValue = newCaps,
                            domainValue = newDomain,
                            nsValue = newNs,
                        )
                    },
                )
            }

            // ── 配置（授权后展开）────────────────────────────────────────────
            item {
                AnimatedVisibility(
                    visible = allowed,
                    enter = expandVertically() + fadeIn(),
                    exit = shrinkVertically() + fadeOut(),
                ) {
                    CardGroup {
                        item {
                            AppListItem(
                                title = stringResource(R.string.app_root_uid),
                                summary = rootUidText,
                                leading = {
                                    Icon(Icons.Rounded.Badge, contentDescription = null)
                                },
                                onClick = { showUidDialog = true },
                            )
                        }
                        item {
                            AppListItem(
                                title = stringResource(R.string.app_capabilities),
                                summary =
                                    stringResource(
                                        R.string.app_caps_selected,
                                        caps.size,
                                        LinuxCap.entries.size,
                                    ),
                                leading = {
                                    Icon(Icons.Rounded.Tune, contentDescription = null)
                                },
                                onClick = { showCapsSheet = true },
                            )
                        }
                        item {
                            AppListItem(
                                title = stringResource(R.string.app_selinux_domain),
                                summary = domain,
                                leading = {
                                    Icon(Icons.Rounded.Shield, contentDescription = null)
                                },
                                onClick = { showDomainDialog = true },
                            )
                        }
                        item {
                            AppListItem(
                                title = stringResource(R.string.app_mount_namespace),
                                summary = stringResource(ns.labelRes),
                                leading = {
                                    Icon(Icons.Rounded.Layers, contentDescription = null)
                                },
                                onClick = { showNsDialog = true },
                            )
                        }
                    }
                }
            }
        }
    }
}

/**
 * Root 授权开关的「重点卡片」：授权后用 tonal 容器上色，形成页面的视觉锚点。
 */
@Composable
private fun RootAccessCard(
    allowed: Boolean,
    onToggle: (Boolean) -> Unit,
) {
    val containerColor =
        if (allowed) {
            MaterialTheme.colorScheme.primaryContainer
        } else {
            MaterialTheme.colorScheme.surfaceBright
        }
    val contentColor =
        if (allowed) {
            MaterialTheme.colorScheme.onPrimaryContainer
        } else {
            MaterialTheme.colorScheme.onSurface
        }

    AppCard(
        modifier = Modifier.fillMaxWidth(),
        shape = ContinuousCornerShape(28.dp),
        containerColor = containerColor,
        contentColor = contentColor,
    ) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .toggleable(
                        value = allowed,
                        role = Role.Switch,
                        onValueChange = onToggle,
                    ).padding(20.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                imageVector = if (allowed) Icons.Rounded.LockOpen else Icons.Rounded.Lock,
                contentDescription = null,
                modifier = Modifier.size(32.dp),
            )
            Spacer(Modifier.width(16.dp))
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                Text(
                    text = stringResource(R.string.app_allow_root),
                    style = MaterialTheme.typography.titleMediumEmphasized,
                )
                Text(
                    text =
                        if (allowed) {
                            stringResource(R.string.app_status_granted)
                        } else {
                            stringResource(R.string.app_status_denied)
                        },
                    style = MaterialTheme.typography.bodyMedium,
                    color = contentColor.copy(alpha = 0.8f),
                )
            }
            AppSwitch(checked = allowed, onCheckedChange = null)
        }
    }
}
