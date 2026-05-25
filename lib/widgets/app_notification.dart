import 'dart:async';

import 'package:flutter/material.dart';

enum AppNotificationType { info, success, warning, error }

OverlayEntry? _activeNotification;

void showAppNotification(
  BuildContext context, {
  required String message,
  AppNotificationType type = AppNotificationType.info,
  IconData? icon,
  Duration duration = const Duration(seconds: 3),
}) {
  final overlay = Overlay.maybeOf(context, rootOverlay: true);
  if (overlay == null) return;

  _activeNotification?.remove();
  _activeNotification = null;

  late final OverlayEntry entry;
  entry = OverlayEntry(
    builder: (context) => _AppNotificationOverlay(
      message: message,
      type: type,
      icon: icon,
      duration: duration,
      onDismissed: () {
        if (_activeNotification == entry) {
          _activeNotification = null;
        }
        entry.remove();
      },
    ),
  );

  _activeNotification = entry;
  overlay.insert(entry);
}

class _AppNotificationOverlay extends StatefulWidget {
  final String message;
  final AppNotificationType type;
  final IconData? icon;
  final Duration duration;
  final VoidCallback onDismissed;

  const _AppNotificationOverlay({
    required this.message,
    required this.type,
    required this.icon,
    required this.duration,
    required this.onDismissed,
  });

  @override
  State<_AppNotificationOverlay> createState() =>
      _AppNotificationOverlayState();
}

class _AppNotificationOverlayState extends State<_AppNotificationOverlay>
    with SingleTickerProviderStateMixin {
  late final AnimationController _controller;
  late final Animation<double> _fade;
  late final Animation<Offset> _slide;
  Timer? _timer;
  bool _dismissed = false;

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 180),
      reverseDuration: const Duration(milliseconds: 140),
    );
    _fade = CurvedAnimation(parent: _controller, curve: Curves.easeOutCubic);
    _slide = Tween<Offset>(
      begin: const Offset(0, -0.2),
      end: Offset.zero,
    ).animate(CurvedAnimation(parent: _controller, curve: Curves.easeOutCubic));

    _controller.forward();
    _timer = Timer(widget.duration, _dismiss);
  }

  @override
  void dispose() {
    _timer?.cancel();
    _controller.dispose();
    super.dispose();
  }

  Future<void> _dismiss() async {
    if (_dismissed) return;
    _dismissed = true;
    await _controller.reverse();
    if (mounted) {
      widget.onDismissed();
    }
  }

  @override
  Widget build(BuildContext context) {
    final accent = _accentColor(widget.type);
    final icon = widget.icon ?? _defaultIcon(widget.type);

    return Positioned(
      top: 52,
      left: 16,
      right: 16,
      child: IgnorePointer(
        ignoring: false,
        child: SafeArea(
          bottom: false,
          child: Align(
            alignment: Alignment.topCenter,
            child: SlideTransition(
              position: _slide,
              child: FadeTransition(
                opacity: _fade,
                child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 520),
                  child: Material(
                    color: Colors.transparent,
                    child: Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 12,
                        vertical: 10,
                      ),
                      decoration: BoxDecoration(
                        color: const Color(0xF2020617),
                        borderRadius: BorderRadius.circular(8),
                        border: Border.all(
                          color: accent.withValues(alpha: 0.75),
                        ),
                        boxShadow: [
                          BoxShadow(
                            color: accent.withValues(alpha: 0.18),
                            blurRadius: 18,
                            offset: const Offset(0, 8),
                          ),
                          BoxShadow(
                            color: Colors.black.withValues(alpha: 0.35),
                            blurRadius: 24,
                            offset: const Offset(0, 12),
                          ),
                        ],
                      ),
                      child: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Container(
                            width: 28,
                            height: 28,
                            decoration: BoxDecoration(
                              color: accent.withValues(alpha: 0.16),
                              borderRadius: BorderRadius.circular(6),
                              border: Border.all(
                                color: accent.withValues(alpha: 0.45),
                              ),
                            ),
                            child: Icon(icon, color: accent, size: 17),
                          ),
                          const SizedBox(width: 10),
                          Flexible(
                            child: Text(
                              widget.message,
                              maxLines: 2,
                              overflow: TextOverflow.ellipsis,
                              style: const TextStyle(
                                color: Color(0xFFE0F2FE),
                                fontSize: 12,
                                fontWeight: FontWeight.w600,
                                height: 1.25,
                              ),
                            ),
                          ),
                          const SizedBox(width: 10),
                          InkWell(
                            onTap: _dismiss,
                            borderRadius: BorderRadius.circular(6),
                            child: Padding(
                              padding: const EdgeInsets.all(3),
                              child: Icon(
                                Icons.close,
                                color: const Color(
                                  0xFF94A3B8,
                                ).withValues(alpha: 0.9),
                                size: 15,
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

Color _accentColor(AppNotificationType type) {
  switch (type) {
    case AppNotificationType.success:
      return const Color(0xFF10B981);
    case AppNotificationType.warning:
      return const Color(0xFFF59E0B);
    case AppNotificationType.error:
      return const Color(0xFFEF4444);
    case AppNotificationType.info:
      return const Color(0xFF22D3EE);
  }
}

IconData _defaultIcon(AppNotificationType type) {
  switch (type) {
    case AppNotificationType.success:
      return Icons.check_circle;
    case AppNotificationType.warning:
      return Icons.warning_amber_rounded;
    case AppNotificationType.error:
      return Icons.error_outline;
    case AppNotificationType.info:
      return Icons.info_outline;
  }
}
