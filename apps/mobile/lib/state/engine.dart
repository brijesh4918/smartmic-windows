import 'dart:async';
import 'dart:ffi';

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:path_provider/path_provider.dart';
import 'package:permission_handler/permission_handler.dart';

import '../native/smartmic_ffi.dart';

/// Owns the native engine and exposes its state to the widget tree.
///
/// State is polled rather than pushed: the native side is lock-free and
/// atomic, and a 60 ms poll is cheaper and far less error-prone than marshalling
/// callbacks across the FFI boundary into Dart's isolate.
class SmartMicEngine extends ChangeNotifier with WidgetsBindingObserver {
  Pointer<Void>? _handle;
  Timer? _poll;

  ConnState conn = ConnState.idle;
  PttState ptt = PttState.ready;
  int rttMs = -1;
  double micLevel = 0;
  String fingerprint = '';
  String deviceId = '';
  String error = '';
  String pcName = '';
  int mode = 0;

  bool get connected => conn == ConnState.authenticated;
  bool get isLive => ptt == PttState.live;

  Future<bool> requestMicrophone() async {
    final status = await Permission.microphone.request();
    if (!status.isGranted) {
      error = 'SmartMic needs the microphone to send your voice to the PC.';
      notifyListeners();
    }
    return status.isGranted;
  }

  Future<void> connect({
    required String host,
    required int port,
    required String sessionId,
    required String code,
  }) async {
    disconnect();

    if (!await requestMicrophone()) return;

    final dir = await getApplicationSupportDirectory();
    _handle = SmartMicNative.instance.create(
      host: host,
      port: port,
      sessionId: sessionId,
      code: code,
      identityDir: dir.path,
    );

    if (_handle == null || _handle == nullptr) {
      error = 'Could not start the SmartMic engine.';
      notifyListeners();
      return;
    }

    pcName = host;
    WidgetsBinding.instance.addObserver(this);
    _poll = Timer.periodic(const Duration(milliseconds: 60), (_) => _tick());
    notifyListeners();
  }

  void disconnect() {
    _poll?.cancel();
    _poll = null;
    final h = _handle;
    _handle = null;
    if (h != null && h != nullptr) SmartMicNative.instance.destroy(h);
    conn = ConnState.idle;
    ptt = PttState.ready;
    rttMs = -1;
    micLevel = 0;
    notifyListeners();
  }

  void _tick() {
    final h = _handle;
    if (h == null || h == nullptr) return;
    final n = SmartMicNative.instance;

    final newConn = n.connState(h);
    final newPtt = n.pttState(h);
    final newLevel = n.micLevel(h);
    final newRtt = n.rttMs(h);

    // One crisp impact when the PC actually accepts the microphone, one softer
    // when it hands control back. Nothing on CONNECTING: buzzing there would
    // teach people to trust the wrong moment.
    switch (n.takeHaptic(h)) {
      case HapticCue.wentLive:
        HapticFeedback.mediumImpact();
        break;
      case HapticCue.returned:
        HapticFeedback.selectionClick();
        break;
      case HapticCue.failed:
        HapticFeedback.heavyImpact();
        break;
      case HapticCue.none:
        break;
    }

    final changed = newConn != conn ||
        newPtt != ptt ||
        newRtt != rttMs ||
        (newLevel - micLevel).abs() > 0.01;

    conn = newConn;
    ptt = newPtt;
    rttMs = newRtt;
    micLevel = newLevel;
    if (conn == ConnState.authenticated && fingerprint.isEmpty) {
      fingerprint = n.fingerprint(h);
    }
    deviceId = n.deviceId(h);
    final e = n.lastError(h);
    if (e.isNotEmpty && e != error) {
      error = e;
      notifyListeners();
      return;
    }
    if (changed) notifyListeners();
  }

  void press() {
    final h = _handle;
    if (h != null && h != nullptr) SmartMicNative.instance.press(h);
  }

  /// Every path that loses the touch funnels here. A release must never be a
  /// no-op that leaves the microphone open on the PC.
  void release() {
    final h = _handle;
    if (h != null && h != nullptr) SmartMicNative.instance.release(h);
  }

  void setMode(int m) {
    final h = _handle;
    if (h != null && h != nullptr) {
      SmartMicNative.instance.setMode(h, m);
      mode = m;
      notifyListeners();
    }
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    // Backgrounded, a call arrives, the screen locks: all of these are
    // finger-up as far as the microphone is concerned.
    if (state != AppLifecycleState.resumed) release();
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    disconnect();
    super.dispose();
  }
}
