import 'package:flutter/material.dart';

import '../native/smartmic_ffi.dart';
import '../state/engine.dart';
import '../theme.dart';
import '../widgets/ptt_button.dart';

class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key, required this.engine});
  final SmartMicEngine engine;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: engine,
      builder: (context, _) {
        return Scaffold(
          body: SafeArea(
            child: Padding(
              padding: const EdgeInsets.fromLTRB(20, 12, 20, 20),
              child: Column(
                children: [
                  _Header(engine: engine),
                  const SizedBox(height: 18),
                  _PcCard(engine: engine),
                  const Spacer(),
                  PttButton(
                    state: engine.ptt,
                    level: engine.micLevel,
                    enabled: engine.connected,
                    onPress: engine.press,
                    onRelease: engine.release,
                  ),
                  const SizedBox(height: 18),
                  _StatusLine(engine: engine),
                  const Spacer(),
                  _ModeRow(engine: engine),
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}

class _Header extends StatelessWidget {
  const _Header({required this.engine});
  final SmartMicEngine engine;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Text('SmartMic', style: Theme.of(context).textTheme.titleLarge),
        const Spacer(),
        IconButton(
          onPressed: () {
            engine.disconnect();
            Navigator.of(context).pop();
          },
          icon: const Icon(Icons.link_off_rounded, color: SmartMicTheme.textDim),
          tooltip: 'Disconnect',
        ),
      ],
    );
  }
}

class _PcCard extends StatelessWidget {
  const _PcCard({required this.engine});
  final SmartMicEngine engine;

  @override
  Widget build(BuildContext context) {
    final connected = engine.connected;
    final color = connected ? SmartMicTheme.live : SmartMicTheme.warn;
    final label = switch (engine.conn) {
      ConnState.authenticated => 'Connected',
      ConnState.pairing => 'Pairing…',
      ConnState.failed => 'Not connected',
      ConnState.idle => 'Idle',
    };

    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: SmartMicTheme.surface,
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: SmartMicTheme.border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 9,
                height: 9,
                decoration: BoxDecoration(color: color, shape: BoxShape.circle),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: Text(engine.pcName.isEmpty ? 'PC' : engine.pcName,
                    style: Theme.of(context).textTheme.titleMedium,
                    overflow: TextOverflow.ellipsis),
              ),
              if (connected && engine.rttMs >= 0)
                Text('${engine.rttMs} ms',
                    style: const TextStyle(color: SmartMicTheme.textDim, fontSize: 13)),
            ],
          ),
          const SizedBox(height: 6),
          Text(label, style: Theme.of(context).textTheme.bodyMedium),
          if (engine.fingerprint.isNotEmpty) ...[
            const SizedBox(height: 10),
            Row(
              children: [
                const Icon(Icons.verified_user_outlined,
                    size: 15, color: SmartMicTheme.textDim),
                const SizedBox(width: 6),
                Text(engine.fingerprint,
                    style: const TextStyle(
                        color: SmartMicTheme.textDim, fontFamily: 'monospace', fontSize: 12)),
              ],
            ),
          ],
          // While pairing has not completed, show whether this phone is even
          // managing to transmit. Without it, "Pairing..." forever is
          // indistinguishable from a dead network.
          if (!connected) ...[
            const SizedBox(height: 10),
            Row(
              children: [
                const Icon(Icons.swap_vert_rounded, size: 15, color: SmartMicTheme.textDim),
                const SizedBox(width: 6),
                Expanded(
                  child: Text(engine.net.diagnosis,
                      style: const TextStyle(color: SmartMicTheme.textDim, fontSize: 12)),
                ),
              ],
            ),
          ],
          if (engine.error.isNotEmpty) ...[
            const SizedBox(height: 12),
            Text(engine.error,
                style: const TextStyle(color: SmartMicTheme.danger, fontSize: 13)),
          ],
        ],
      ),
    );
  }
}

class _StatusLine extends StatelessWidget {
  const _StatusLine({required this.engine});
  final SmartMicEngine engine;

  @override
  Widget build(BuildContext context) {
    final text = switch (engine.ptt) {
      PttState.live => 'Your phone is the microphone',
      PttState.connecting => 'Waiting for the PC to accept…',
      PttState.releasing => 'Handing back to the PC microphone',
      PttState.failed => 'The PC did not take the microphone',
      PttState.ready =>
        engine.connected ? 'Using the PC microphone' : 'Not connected',
    };
    return Text(
      text,
      textAlign: TextAlign.center,
      style: TextStyle(
        color: engine.ptt == PttState.failed
            ? SmartMicTheme.danger
            : SmartMicTheme.textDim,
        fontSize: 14,
      ),
    );
  }
}

/// Remote control for the PC's routing mode. Useful even when nobody is
/// holding the button: it is how you mute a PC from across the room.
class _ModeRow extends StatelessWidget {
  const _ModeRow({required this.engine});
  final SmartMicEngine engine;

  static const _modes = ['Auto', 'Phone', 'PC only', 'Mute'];

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('PC microphone mode', style: Theme.of(context).textTheme.bodyMedium),
        const SizedBox(height: 10),
        Row(
          children: List.generate(_modes.length, (i) {
            final selected = engine.mode == i;
            return Expanded(
              child: Padding(
                padding: EdgeInsets.only(right: i == _modes.length - 1 ? 0 : 8),
                child: GestureDetector(
                  onTap: engine.connected ? () => engine.setMode(i) : null,
                  child: AnimatedContainer(
                    duration: const Duration(milliseconds: 140),
                    padding: const EdgeInsets.symmetric(vertical: 12),
                    decoration: BoxDecoration(
                      color: selected
                          ? SmartMicTheme.accent.withValues(alpha: 0.16)
                          : SmartMicTheme.surface,
                      borderRadius: BorderRadius.circular(12),
                      border: Border.all(
                        color: selected ? SmartMicTheme.accent : SmartMicTheme.border,
                      ),
                    ),
                    alignment: Alignment.center,
                    child: Text(
                      _modes[i],
                      style: TextStyle(
                        color: selected ? SmartMicTheme.accent : SmartMicTheme.textDim,
                        fontSize: 13,
                        fontWeight: selected ? FontWeight.w700 : FontWeight.w500,
                      ),
                    ),
                  ),
                ),
              ),
            );
          }),
        ),
      ],
    );
  }
}
