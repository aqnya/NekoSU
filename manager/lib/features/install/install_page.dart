import 'package:flutter/material.dart';
import 'package:nekosu/core/install_bridge.dart';
import 'package:nekosu/l10n/app_localizations.dart';
import 'package:nekosu/shared/widgets/card_group.dart';
import 'package:nekosu/shared/widgets/output_page.dart';

class InstallPage extends StatefulWidget {
  const InstallPage({super.key});

  @override
  State<InstallPage> createState() => _InstallPageState();
}

class _InstallPageState extends State<InstallPage> {
  InstallEnvironment? _environment;
  InstallOperation _operation = InstallOperation.file;
  StagedBootImage? _vendorBoot;
  LocalKernelModule? _localKo;
  String? _selectedKmi;
  String? _loadError;
  bool _loading = true;
  bool _picking = false;
  bool _starting = false;

  @override
  void initState() {
    super.initState();
    _loadEnvironment();
  }

  Future<void> _loadEnvironment() async {
    setState(() {
      _loading = true;
      _loadError = null;
    });
    try {
      final environment = await InstallBridge.environment();
      if (!mounted) return;
      setState(() {
        _environment = environment;
        _loading = false;
        if (!_operationsFor(environment).contains(_operation)) {
          _operation = InstallOperation.file;
        }
      });
    } catch (error) {
      if (!mounted) return;
      setState(() {
        _loading = false;
        _loadError = error.toString();
      });
    }
  }

  List<InstallOperation> _operationsFor(InstallEnvironment environment) {
    return [
      InstallOperation.file,
      if (environment.rootAvailable && environment.currentVendorBoot != null)
        InstallOperation.direct,
      if (environment.rootAvailable && environment.inactiveVendorBoot != null)
        InstallOperation.inactive,
    ];
  }

  String? get _selectedKoName {
    final environment = _environment;
    if (environment == null) return null;
    final selectedKmi = _selectedKmi;
    if (selectedKmi != null) {
      for (final candidate in environment.koAssets) {
        if (InstallBridge.kmiOf(candidate) == selectedKmi) return candidate;
      }
    }
    return environment.matchedKo;
  }

  String? get _displayKmi {
    final selectedKmi = _selectedKmi;
    if (selectedKmi != null) return selectedKmi;
    return _environment?.matchedKmi;
  }

  bool get _canInstall {
    final environment = _environment;
    if (environment == null ||
        _loading ||
        _picking ||
        _starting ||
        !environment.bootpatcherAvailable) {
      return false;
    }
    if (_localKo == null && _selectedKoName == null) return false;
    return switch (_operation) {
      InstallOperation.file => _vendorBoot != null,
      InstallOperation.direct =>
        environment.rootAvailable && environment.currentVendorBoot != null,
      InstallOperation.inactive =>
        environment.rootAvailable && environment.inactiveVendorBoot != null,
    };
  }

  void _showMessage(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(SnackBar(content: Text(message)));
  }

  Future<void> _pickVendorBoot() async {
    if (_picking || _starting) return;
    final l10n = AppLocalizations.of(context)!;
    setState(() => _picking = true);
    try {
      final image = await InstallBridge.pickVendorBoot();
      if (!mounted || image == null) return;
      setState(() {
        _vendorBoot = image;
        _operation = InstallOperation.file;
      });
    } catch (_) {
      if (mounted) _showMessage(l10n.installPickerFailed);
    } finally {
      if (mounted) setState(() => _picking = false);
    }
  }

  Future<void> _pickLocalKo() async {
    if (_picking || _starting) return;
    final l10n = AppLocalizations.of(context)!;
    setState(() => _picking = true);
    try {
      final module = await InstallBridge.pickLocalKo();
      if (!mounted || module == null) return;
      setState(() => _localKo = module);
    } catch (_) {
      if (mounted) _showMessage(l10n.installPickerFailed);
    } finally {
      if (mounted) setState(() => _picking = false);
    }
  }

  Future<void> _selectKmi() async {
    final environment = _environment;
    if (environment == null || environment.koAssets.isEmpty) return;
    final l10n = AppLocalizations.of(context)!;
    final kmis =
        environment.koAssets
            .map(InstallBridge.kmiOf)
            .whereType<String>()
            .toSet()
            .toList()
          ..sort();
    if (kmis.isEmpty) return;

    const autoValue = '\u0000auto';
    final selected = await showDialog<String>(
      context: context,
      builder: (dialogContext) {
        final scheme = Theme.of(dialogContext).colorScheme;
        return AlertDialog(
          title: Text(l10n.installSelectKmi),
          content: SizedBox(
            width: double.maxFinite,
            child: ListView(
              shrinkWrap: true,
              children: [
                ListTile(
                  leading: Icon(
                    _selectedKmi == null
                        ? Icons.check_circle_rounded
                        : Icons.circle_outlined,
                    color: _selectedKmi == null ? scheme.primary : null,
                  ),
                  title: Text(l10n.installAutoOption),
                  subtitle: environment.matchedKmi == null
                      ? null
                      : Text(environment.matchedKmi!),
                  onTap: () => Navigator.of(dialogContext).pop(autoValue),
                ),
                for (final kmi in kmis)
                  ListTile(
                    leading: Icon(
                      _selectedKmi == kmi
                          ? Icons.check_circle_rounded
                          : Icons.circle_outlined,
                      color: _selectedKmi == kmi ? scheme.primary : null,
                    ),
                    title: Text(kmi),
                    subtitle: kmi == environment.matchedKmi
                        ? Text(l10n.installRecommended)
                        : null,
                    onTap: () => Navigator.of(dialogContext).pop(kmi),
                  ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.of(dialogContext).pop(),
              child: Text(l10n.dialogCancel),
            ),
          ],
        );
      },
    );
    if (!mounted || selected == null) return;
    setState(() {
      _selectedKmi = selected == autoValue ? null : selected;
      _localKo = null;
    });
  }

  void _selectOperation(InstallOperation operation) {
    if (_starting) return;
    if (operation == InstallOperation.file &&
        _operation == InstallOperation.file) {
      _pickVendorBoot();
      return;
    }
    setState(() => _operation = operation);
  }

  Future<void> _startInstall() async {
    final environment = _environment;
    final l10n = AppLocalizations.of(context)!;
    if (environment == null) return;
    if (!environment.bootpatcherAvailable) {
      _showMessage(l10n.installBootpatcherMissing);
      return;
    }
    if (_operation == InstallOperation.file && _vendorBoot == null) {
      _showMessage(l10n.installBootImageRequired);
      return;
    }
    if (_localKo == null && _selectedKoName == null) {
      _showMessage(l10n.installKoRequired);
      return;
    }

    final partition = switch (_operation) {
      InstallOperation.file => null,
      InstallOperation.direct => environment.currentVendorBoot,
      InstallOperation.inactive => environment.inactiveVendorBoot,
    };
    if (_operation != InstallOperation.file) {
      final confirmed = await showDialog<bool>(
        context: context,
        builder: (dialogContext) => AlertDialog(
          title: Text(
            _operation == InstallOperation.direct
                ? l10n.installDirectConfirmTitle
                : l10n.installInactiveConfirmTitle,
          ),
          content: Text(
            _operation == InstallOperation.direct
                ? l10n.installDirectConfirmMessage(partition ?? '')
                : l10n.installInactiveConfirmMessage(partition ?? ''),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.of(dialogContext).pop(false),
              child: Text(l10n.dialogCancel),
            ),
            FilledButton(
              onPressed: () => Navigator.of(dialogContext).pop(true),
              child: Text(l10n.installConfirmFlash),
            ),
          ],
        ),
      );
      if (!mounted || confirmed != true) return;
    }

    final request = InstallRequest(
      operation: _operation,
      vendorBootPath: _operation == InstallOperation.file
          ? _vendorBoot?.path
          : null,
      koName: _localKo == null ? _selectedKoName : null,
      koPath: _localKo?.path,
      partition: partition,
    );

    setState(() => _starting = true);
    var succeeded = false;
    try {
      await Navigator.of(context).push<void>(
        MaterialPageRoute(
          builder: (context) => OutputPage(
            task: () => InstallBridge.output(request),
            runningTitle: l10n.installRunning,
            successTitle: l10n.installSucceeded,
            failureTitle: l10n.installFailed,
            logFileName: 'nekosu-install',
            onFinished: (status) {
              succeeded = status == OutputStatus.succeeded;
            },
            completionAction: OutputAction(
              label: l10n.dialogDone,
              icon: Icons.check_rounded,
              onPressed: (context) => Navigator.of(context).pop(),
            ),
          ),
        ),
      );
    } finally {
      if (mounted) setState(() => _starting = false);
    }

    if (!mounted) return;
    if (succeeded) {
      Navigator.of(context).pop(true);
      return;
    }
    await _loadEnvironment();
  }

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final l10n = AppLocalizations.of(context)!;
    final environment = _environment;

    return Scaffold(
      backgroundColor: scheme.surfaceContainer,
      appBar: AppBar(
        title: Text(l10n.installTitle),
        backgroundColor: scheme.surfaceContainer,
        surfaceTintColor: Colors.transparent,
        scrolledUnderElevation: 0,
      ),
      body: _buildBody(context, l10n, environment),
      bottomNavigationBar: environment == null
          ? null
          : SafeArea(
              minimum: const EdgeInsets.fromLTRB(16, 8, 16, 16),
              child: FilledButton.icon(
                onPressed: _canInstall ? _startInstall : null,
                icon: _starting || _picking
                    ? const SizedBox.square(
                        dimension: 20,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : const Icon(Icons.play_arrow_rounded),
                label: Text(l10n.installStart),
                style: FilledButton.styleFrom(
                  minimumSize: const Size.fromHeight(54),
                ),
              ),
            ),
    );
  }

  Widget _buildBody(
    BuildContext context,
    AppLocalizations l10n,
    InstallEnvironment? environment,
  ) {
    if (environment == null) {
      if (_loading) {
        return const Center(child: CircularProgressIndicator());
      }
      return _LoadErrorView(
        message: l10n.installLoadFailed,
        detail: _loadError,
        retryLabel: l10n.retry,
        onRetry: _loadEnvironment,
      );
    }

    final scheme = Theme.of(context).colorScheme;
    final operations = _operationsFor(environment);
    return ListView(
      padding: const EdgeInsets.only(top: 8, bottom: 24),
      children: [
        _SectionLabel(text: l10n.installDeviceInfoSection),
        const SizedBox(height: 10),
        CardGroup(
          children: [
            ListRow(
              icon: const Icon(Icons.memory_rounded),
              headline: Text(l10n.installCurrentKmi),
              supporting: Text(environment.currentKmi ?? l10n.unknownValue),
            ),
            ListRow(
              icon: const Icon(Icons.terminal_rounded),
              headline: Text(l10n.installKernelRelease),
              supporting: Text(
                environment.kernelRelease,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
            ),
            ListRow(
              icon: const Icon(Icons.android_rounded),
              headline: Text(l10n.installAndroidVersion),
              supporting: Text(environment.androidVersion),
            ),
          ],
        ),
        const SizedBox(height: 18),
        _SectionLabel(text: l10n.installMethodSection),
        const SizedBox(height: 10),
        CardGroup(
          children: [
            for (final operation in operations)
              _OperationRow(
                operation: operation,
                selected: _operation == operation,
                title: switch (operation) {
                  InstallOperation.file => l10n.installMethodFile,
                  InstallOperation.direct => l10n.installMethodDirect,
                  InstallOperation.inactive => l10n.installMethodInactive,
                },
                description: _operationDescription(
                  l10n,
                  environment,
                  operation,
                ),
                onTap: () => _selectOperation(operation),
              ),
          ],
        ),
        const SizedBox(height: 18),
        _SectionLabel(text: l10n.installKernelModuleSection),
        const SizedBox(height: 10),
        CardGroup(
          children: [
            ListRow(
              icon: const Icon(Icons.developer_board_rounded),
              headline: Text(_displayKmi ?? l10n.installNoMatchingKmi),
              supporting: Text(
                _displayKmi == null
                    ? l10n.installKoMissing
                    : _selectedKmi == null
                    ? l10n.installAutoMatched(_displayKmi!)
                    : l10n.installBundledLkm,
              ),
              trailing: const Icon(Icons.chevron_right_rounded),
              onTap: environment.koAssets.isEmpty ? null : _selectKmi,
            ),
            ListRow(
              icon: const Icon(Icons.folder_open_rounded),
              headline: Text(_localKo?.name ?? l10n.installUseLocalLkm),
              supporting: Text(
                _localKo == null
                    ? l10n.installUseLocalLkmDescription
                    : l10n.installLocalLkmDescription,
              ),
              trailing: _localKo == null
                  ? const Icon(Icons.chevron_right_rounded)
                  : IconButton(
                      tooltip: l10n.dialogClear,
                      onPressed: () => setState(() => _localKo = null),
                      icon: const Icon(Icons.close_rounded),
                    ),
              onTap: _localKo == null ? _pickLocalKo : null,
            ),
          ],
        ),
        if (!environment.bootpatcherAvailable) ...[
          const SizedBox(height: 18),
          _Notice(
            text: l10n.installBootpatcherMissing,
            color: scheme.errorContainer,
            contentColor: scheme.onErrorContainer,
          ),
        ],
        const SizedBox(height: 18),
        _Notice(
          text: l10n.installVendorBootHint,
          color: scheme.secondaryContainer,
          contentColor: scheme.onSecondaryContainer,
        ),
      ],
    );
  }

  String _operationDescription(
    AppLocalizations l10n,
    InstallEnvironment environment,
    InstallOperation operation,
  ) {
    return switch (operation) {
      InstallOperation.file =>
        _vendorBoot?.name ?? l10n.installMethodFileDescription,
      InstallOperation.direct =>
        environment.currentVendorBoot ?? l10n.installMethodDirectDescription,
      InstallOperation.inactive =>
        environment.inactiveVendorBoot ?? l10n.installMethodInactiveDescription,
    };
  }
}

class _OperationRow extends StatelessWidget {
  const _OperationRow({
    required this.operation,
    required this.selected,
    required this.title,
    required this.description,
    required this.onTap,
  });

  final InstallOperation operation;
  final bool selected;
  final String title;
  final String description;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return ListRow(
      icon: Icon(
        selected
            ? Icons.radio_button_checked_rounded
            : Icons.radio_button_unchecked_rounded,
        color: selected ? scheme.primary : null,
      ),
      headline: Text(title),
      supporting: Text(
        description,
        maxLines: 2,
        overflow: TextOverflow.ellipsis,
      ),
      trailing: Icon(
        operation == InstallOperation.file
            ? Icons.image_outlined
            : Icons.smartphone_rounded,
      ),
      onTap: onTap,
    );
  }
}

class _SectionLabel extends StatelessWidget {
  const _SectionLabel({required this.text});

  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(left: 16, top: 4),
      child: Text(
        text,
        style: Theme.of(context).textTheme.titleSmall
            ?.copyWith(color: Theme.of(context).colorScheme.primary),
      ),
    );
  }
}

class _Notice extends StatelessWidget {
  const _Notice({
    required this.text,
    required this.color,
    required this.contentColor,
  });

  final String text;
  final Color color;
  final Color contentColor;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16),
      child: Container(
        padding: const EdgeInsets.all(16),
        decoration: BoxDecoration(
          color: color,
          borderRadius: BorderRadius.circular(20),
        ),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Icon(Icons.info_outline_rounded, color: contentColor, size: 20),
            const SizedBox(width: 12),
            Expanded(
              child: Text(
                text,
                style: Theme.of(context).textTheme.bodyMedium
                    ?.copyWith(color: contentColor),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _LoadErrorView extends StatelessWidget {
  const _LoadErrorView({
    required this.message,
    required this.detail,
    required this.retryLabel,
    required this.onRetry,
  });

  final String message;
  final String? detail;
  final String retryLabel;
  final VoidCallback onRetry;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.error_outline_rounded, color: scheme.error, size: 40),
            const SizedBox(height: 16),
            Text(message, style: Theme.of(context).textTheme.titleMedium),
            if (detail != null && detail!.isNotEmpty) ...[
              const SizedBox(height: 8),
              Text(
                detail!,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodySmall
                    ?.copyWith(color: scheme.onSurfaceVariant),
              ),
            ],
            const SizedBox(height: 20),
            FilledButton(onPressed: onRetry, child: Text(retryLabel)),
          ],
        ),
      ),
    );
  }
}
