import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../native/smartmic_ffi.dart';
import '../state/engine.dart';
import '../theme.dart';
import 'home_screen.dart';

class PairScreen extends StatefulWidget {
  const PairScreen({super.key, required this.engine});
  final SmartMicEngine engine;

  @override
  State<PairScreen> createState() => _PairScreenState();
}

class _PairScreenState extends State<PairScreen> {
  final _host = TextEditingController();
  final _port = TextEditingController(text: '47820');
  final _session = TextEditingController();
  final _code = TextEditingController();
  bool _busy = false;
  String? _fingerprint;

  @override
  void dispose() {
    _host.dispose();
    _port.dispose();
    _session.dispose();
    _code.dispose();
    super.dispose();
  }

  /// The PC prints a smartmic:// link next to the code. Pasting it fills in
  /// everything, including the key fingerprint that makes pairing safe on a
  /// network you do not trust.
  Future<void> _pasteLink() async {
    final data = await Clipboard.getData(Clipboard.kTextPlain);
    final text = data?.text?.trim();
    if (text == null || text.isEmpty) return;
    final parsed = SmartMicNative.instance.parseUri(text);
    if (parsed == null) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('That does not look like a SmartMic pairing link.')),
      );
      return;
    }
    setState(() {
      _host.text = parsed.host;
      _port.text = parsed.port.toString();
      _session.text = parsed.sessionId;
      _code.text = parsed.code;
      _fingerprint = parsed.fingerprint;
    });
  }

  Future<void> _connect() async {
    setState(() => _busy = true);
    await widget.engine.connect(
      host: _host.text.trim(),
      port: int.tryParse(_port.text.trim()) ?? 47820,
      sessionId: _session.text.trim(),
      code: _code.text.trim(),
    );
    if (!mounted) return;
    setState(() => _busy = false);
    Navigator.of(context).push(
      MaterialPageRoute(builder: (_) => HomeScreen(engine: widget.engine)),
    );
  }

  bool get _ready =>
      _host.text.trim().isNotEmpty &&
      _session.text.trim().isNotEmpty &&
      _code.text.trim().length == 6;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.fromLTRB(24, 32, 24, 32),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Container(
                    width: 44,
                    height: 44,
                    decoration: BoxDecoration(
                      color: SmartMicTheme.accent.withValues(alpha: 0.15),
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: const Icon(Icons.mic_external_on, color: SmartMicTheme.accent),
                  ),
                  const SizedBox(width: 14),
                  Text('SmartMic', style: Theme.of(context).textTheme.displaySmall),
                ],
              ),
              const SizedBox(height: 28),
              Text('Pair with your PC', style: Theme.of(context).textTheme.titleLarge),
              const SizedBox(height: 8),
              Text(
                'Run SmartMic on your computer. It shows a six-digit code and a '
                'pairing link. Paste the link, or type the details in.',
                style: Theme.of(context).textTheme.bodyMedium,
              ),
              const SizedBox(height: 22),
              OutlinedButton.icon(
                onPressed: _pasteLink,
                icon: const Icon(Icons.content_paste_rounded),
                label: const Text('Paste pairing link'),
                style: OutlinedButton.styleFrom(
                  minimumSize: const Size.fromHeight(52),
                  foregroundColor: SmartMicTheme.accent,
                  side: const BorderSide(color: SmartMicTheme.border),
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
                ),
              ),
              const SizedBox(height: 24),
              const _Divider(),
              const SizedBox(height: 24),
              Row(
                children: [
                  Expanded(
                    flex: 3,
                    child: TextField(
                      controller: _host,
                      decoration: const InputDecoration(labelText: 'PC address'),
                      onChanged: (_) => setState(() {}),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: TextField(
                      controller: _port,
                      keyboardType: TextInputType.number,
                      decoration: const InputDecoration(labelText: 'Port'),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 14),
              TextField(
                controller: _session,
                decoration: const InputDecoration(labelText: 'Session'),
                onChanged: (_) => setState(() {}),
              ),
              const SizedBox(height: 14),
              TextField(
                controller: _code,
                keyboardType: TextInputType.number,
                maxLength: 6,
                style: const TextStyle(
                  fontSize: 26,
                  letterSpacing: 10,
                  fontWeight: FontWeight.w700,
                  color: SmartMicTheme.text,
                ),
                decoration: const InputDecoration(
                  labelText: 'Pairing code',
                  counterText: '',
                ),
                onChanged: (_) => setState(() {}),
              ),
              if (_fingerprint != null) ...[
                const SizedBox(height: 18),
                _FingerprintCard(fingerprint: _fingerprint!),
              ],
              if (widget.engine.error.isNotEmpty) ...[
                const SizedBox(height: 18),
                Text(widget.engine.error,
                    style: const TextStyle(color: SmartMicTheme.danger)),
              ],
              const SizedBox(height: 26),
              FilledButton(
                onPressed: (_ready && !_busy) ? _connect : null,
                child: _busy
                    ? const SizedBox(
                        width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2))
                    : const Text('Pair'),
              ),
              const SizedBox(height: 14),
              Text(
                'The code works once and expires after five minutes. It is never '
                'sent over the network.',
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(fontSize: 12),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _FingerprintCard extends StatelessWidget {
  const _FingerprintCard({required this.fingerprint});
  final String fingerprint;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: SmartMicTheme.surface,
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: SmartMicTheme.border),
      ),
      child: Row(
        children: [
          const Icon(Icons.verified_user_outlined, color: SmartMicTheme.live, size: 20),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('PC fingerprint', style: Theme.of(context).textTheme.bodyMedium),
                const SizedBox(height: 4),
                Text(fingerprint,
                    style: const TextStyle(
                        color: SmartMicTheme.text,
                        fontFamily: 'monospace',
                        fontSize: 15,
                        letterSpacing: 1)),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _Divider extends StatelessWidget {
  const _Divider();

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        const Expanded(child: Divider(color: SmartMicTheme.border)),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12),
          child: Text('or enter manually',
              style: Theme.of(context).textTheme.bodyMedium?.copyWith(fontSize: 12)),
        ),
        const Expanded(child: Divider(color: SmartMicTheme.border)),
      ],
    );
  }
}
