// FFI bindings to libsmartmic_phone.so.
//
// The native side owns pairing, Opus, encryption, the PTT timing rules and the
// microphone. Dart owns the screen. Nothing here blocks: every call returns
// immediately and state is polled (ADR-009).
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

// Keep these in lockstep with smartmic_phone_api.h.
enum ConnState { idle, pairing, authenticated, failed }

enum PttState { ready, connecting, live, releasing, failed }

enum HapticCue { none, wentLive, returned, failed }

typedef _CreateNative = Pointer<Void> Function(
    Pointer<Utf8>, Uint16, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);
typedef _CreateDart = Pointer<Void> Function(
    Pointer<Utf8>, int, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);

typedef _VoidPtrNative = Void Function(Pointer<Void>);
typedef _VoidPtrDart = void Function(Pointer<Void>);

typedef _IntPtrNative = Int32 Function(Pointer<Void>);
typedef _IntPtrDart = int Function(Pointer<Void>);

typedef _FloatPtrNative = Float Function(Pointer<Void>);
typedef _FloatPtrDart = double Function(Pointer<Void>);

typedef _StrPtrNative = Pointer<Utf8> Function(Pointer<Void>);
typedef _StrPtrDart = Pointer<Utf8> Function(Pointer<Void>);

typedef _SetModeNative = Void Function(Pointer<Void>, Int32);
typedef _SetModeDart = void Function(Pointer<Void>, int);

typedef _ParseUriNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Int32, Pointer<Uint16>,
    Pointer<Utf8>, Int32, Pointer<Utf8>, Int32, Pointer<Utf8>, Int32);
typedef _ParseUriDart = int Function(Pointer<Utf8>, Pointer<Utf8>, int, Pointer<Uint16>,
    Pointer<Utf8>, int, Pointer<Utf8>, int, Pointer<Utf8>, int);

class PairingUri {
  PairingUri(this.host, this.port, this.sessionId, this.code, this.fingerprint);
  final String host;
  final int port;
  final String sessionId;
  final String code;
  final String fingerprint;
}

class SmartMicNative {
  SmartMicNative._(this._lib) {
    _create = _lib.lookupFunction<_CreateNative, _CreateDart>('sm_phone_create');
    _destroy = _lib.lookupFunction<_VoidPtrNative, _VoidPtrDart>('sm_phone_destroy');
    _connState = _lib.lookupFunction<_IntPtrNative, _IntPtrDart>('sm_phone_conn_state');
    _pttState = _lib.lookupFunction<_IntPtrNative, _IntPtrDart>('sm_phone_ptt_state');
    _rtt = _lib.lookupFunction<_IntPtrNative, _IntPtrDart>('sm_phone_rtt_ms');
    _level = _lib.lookupFunction<_FloatPtrNative, _FloatPtrDart>('sm_phone_mic_level');
    _haptic = _lib.lookupFunction<_IntPtrNative, _IntPtrDart>('sm_phone_take_haptic');
    _press = _lib.lookupFunction<_VoidPtrNative, _VoidPtrDart>('sm_phone_press');
    _release = _lib.lookupFunction<_VoidPtrNative, _VoidPtrDart>('sm_phone_release');
    _setMode = _lib.lookupFunction<_SetModeNative, _SetModeDart>('sm_phone_set_mode');
    _fingerprint =
        _lib.lookupFunction<_StrPtrNative, _StrPtrDart>('sm_phone_peer_fingerprint');
    _deviceId = _lib.lookupFunction<_StrPtrNative, _StrPtrDart>('sm_phone_device_id');
    _lastError = _lib.lookupFunction<_StrPtrNative, _StrPtrDart>('sm_phone_last_error');
    _pcStatus = _lib.lookupFunction<_StrPtrNative, _StrPtrDart>('sm_phone_pc_status');
    _parseUri = _lib.lookupFunction<_ParseUriNative, _ParseUriDart>('sm_phone_parse_uri');
  }

  static SmartMicNative? _instance;

  static SmartMicNative get instance {
    _instance ??= SmartMicNative._(_open());
    return _instance!;
  }

  static DynamicLibrary _open() {
    if (Platform.isAndroid) return DynamicLibrary.open('libsmartmic_phone.so');
    if (Platform.isIOS) return DynamicLibrary.process();
    if (Platform.isMacOS) return DynamicLibrary.open('libsmartmic_phone.dylib');
    throw UnsupportedError('SmartMic has no native core for this platform yet');
  }

  final DynamicLibrary _lib;
  late final _CreateDart _create;
  late final _VoidPtrDart _destroy;
  late final _IntPtrDart _connState;
  late final _IntPtrDart _pttState;
  late final _IntPtrDart _rtt;
  late final _FloatPtrDart _level;
  late final _IntPtrDart _haptic;
  late final _VoidPtrDart _press;
  late final _VoidPtrDart _release;
  late final _SetModeDart _setMode;
  late final _StrPtrDart _fingerprint;
  late final _StrPtrDart _deviceId;
  late final _StrPtrDart _lastError;
  late final _StrPtrDart _pcStatus;
  late final _ParseUriDart _parseUri;

  Pointer<Void> create({
    required String host,
    required int port,
    required String sessionId,
    required String code,
    required String identityDir,
  }) {
    final h = host.toNativeUtf8();
    final s = sessionId.toNativeUtf8();
    final c = code.toNativeUtf8();
    final d = identityDir.toNativeUtf8();
    try {
      return _create(h, port, s, c, d);
    } finally {
      calloc.free(h);
      calloc.free(s);
      calloc.free(c);
      calloc.free(d);
    }
  }

  void destroy(Pointer<Void> p) => _destroy(p);
  ConnState connState(Pointer<Void> p) => ConnState.values[_connState(p).clamp(0, 3)];
  PttState pttState(Pointer<Void> p) => PttState.values[_pttState(p).clamp(0, 4)];
  int rttMs(Pointer<Void> p) => _rtt(p);
  double micLevel(Pointer<Void> p) => _level(p);
  HapticCue takeHaptic(Pointer<Void> p) => HapticCue.values[_haptic(p).clamp(0, 3)];
  void press(Pointer<Void> p) => _press(p);
  void release(Pointer<Void> p) => _release(p);
  void setMode(Pointer<Void> p, int mode) => _setMode(p, mode);
  String fingerprint(Pointer<Void> p) => _fingerprint(p).toDartString();
  String deviceId(Pointer<Void> p) => _deviceId(p).toDartString();
  String lastError(Pointer<Void> p) => _lastError(p).toDartString();
  String pcStatus(Pointer<Void> p) => _pcStatus(p).toDartString();

  /// Parses a scanned or pasted `smartmic://pair?...` link.
  PairingUri? parseUri(String uri) {
    final u = uri.toNativeUtf8();
    final host = calloc<Uint8>(128).cast<Utf8>();
    final session = calloc<Uint8>(128).cast<Utf8>();
    final code = calloc<Uint8>(32).cast<Utf8>();
    final fp = calloc<Uint8>(64).cast<Utf8>();
    final port = calloc<Uint16>();
    try {
      final ok = _parseUri(u, host, 128, port, session, 128, code, 32, fp, 64);
      if (ok != 1) return null;
      return PairingUri(host.toDartString(), port.value, session.toDartString(),
          code.toDartString(), fp.toDartString());
    } finally {
      calloc.free(u);
      calloc.free(host);
      calloc.free(session);
      calloc.free(code);
      calloc.free(fp);
      calloc.free(port);
    }
  }
}
