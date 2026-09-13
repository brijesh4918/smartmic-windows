import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../native/smartmic_ffi.dart';
import '../theme.dart';

/// The hold-to-talk control.
///
/// READY and LIVE must be impossible to confuse at arm's length, and must stay
/// distinguishable for a colour-blind user -- so the difference is fill,
/// weight and motion, not hue alone.
class PttButton extends StatefulWidget {
  const PttButton({
    super.key,
    required this.state,
    required this.level,
    required this.enabled,
    required this.onPress,
    required this.onRelease,
  });

  final PttState state;
  final double level;
  final bool enabled;
  final VoidCallback onPress;
  final VoidCallback onRelease;

  @override
  State<PttButton> createState() => _PttButtonState();
}

class _PttButtonState extends State<PttButton> with SingleTickerProviderStateMixin {
  late final AnimationController _pulse =
      AnimationController(vsync: this, duration: const Duration(milliseconds: 1600))
        ..repeat();

  @override
  void dispose() {
    _pulse.dispose();
    super.dispose();
  }

  Color get _color {
    switch (widget.state) {
      case PttState.live:
        return SmartMicTheme.live;
      case PttState.connecting:
      case PttState.releasing:
        return SmartMicTheme.warn;
      case PttState.failed:
        return SmartMicTheme.danger;
      case PttState.ready:
        return SmartMicTheme.accent;
    }
  }

  String get _label {
    switch (widget.state) {
      case PttState.live:
        return 'LIVE';
      case PttState.connecting:
        return 'CONNECTING';
      case PttState.releasing:
        return 'RELEASING';
      case PttState.failed:
        return 'FAILED';
      case PttState.ready:
        return 'HOLD\nTO TALK';
    }
  }

  @override
  Widget build(BuildContext context) {
    final live = widget.state == PttState.live;
    final color = _color;

    return Listener(
      // Listener rather than GestureDetector: we want the raw pointer, so a
      // drag off the button or a cancelled gesture still ends the transmission.
      onPointerDown: widget.enabled ? (_) => widget.onPress() : null,
      onPointerUp: (_) => widget.onRelease(),
      onPointerCancel: (_) => widget.onRelease(),
      child: AnimatedBuilder(
        animation: _pulse,
        builder: (context, _) {
          final ring = live ? 0.5 + 0.5 * math.sin(_pulse.value * 2 * math.pi) : 0.0;
          return SizedBox(
            width: 248,
            height: 248,
            child: Stack(
              alignment: Alignment.center,
              children: [
                if (live)
                  Container(
                    width: 210 + 34 * ring,
                    height: 210 + 34 * ring,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: color.withValues(alpha: 0.10 * (1 - ring)),
                    ),
                  ),
                // Live level meter: a ring that grows with the voice, so the
                // user can see the microphone is actually picking them up.
                if (live)
                  CustomPaint(
                    size: const Size(224, 224),
                    painter: _LevelRingPainter(
                      level: widget.level.clamp(0.0, 1.0),
                      color: color,
                    ),
                  ),
                AnimatedContainer(
                  duration: const Duration(milliseconds: 160),
                  width: live ? 200 : 188,
                  height: live ? 200 : 188,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: live ? color : SmartMicTheme.surfaceHigh,
                    border: Border.all(
                      color: live ? color : color.withValues(alpha: 0.55),
                      width: live ? 0 : 2.5,
                    ),
                    boxShadow: live
                        ? [BoxShadow(color: color.withValues(alpha: 0.35), blurRadius: 40, spreadRadius: 4)]
                        : null,
                  ),
                  alignment: Alignment.center,
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(
                        live ? Icons.mic : Icons.mic_none,
                        size: 40,
                        color: live ? SmartMicTheme.bg : color,
                      ),
                      const SizedBox(height: 10),
                      Text(
                        _label,
                        textAlign: TextAlign.center,
                        style: TextStyle(
                          color: live ? SmartMicTheme.bg : SmartMicTheme.text,
                          fontSize: live ? 22 : 16,
                          height: 1.15,
                          fontWeight: FontWeight.w800,
                          letterSpacing: 1.2,
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          );
        },
      ),
    );
  }
}

class _LevelRingPainter extends CustomPainter {
  _LevelRingPainter({required this.level, required this.color});

  final double level;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    // A little compression, so quiet speech still moves the ring visibly.
    final amount = math.pow(level, 0.6).toDouble();
    final paint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 3 + 7 * amount
      ..color = color.withValues(alpha: 0.35 + 0.45 * amount);
    canvas.drawCircle(center, size.width / 2 - 6, paint);
  }

  @override
  bool shouldRepaint(_LevelRingPainter old) => old.level != level;
}
