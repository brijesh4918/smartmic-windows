/*  smartmic_phone_api.h
 *
 *  The entire surface the Flutter app sees. Everything below it -- pairing,
 *  Opus, encryption, the PTT state model, microphone capture -- is the same
 *  C++ the desktop service and the test suite use (ADR-009: the UI is
 *  cross-platform, the parts that fail in platform-specific ways are native).
 *
 *  Every function is safe to call from Dart's main isolate: nothing here
 *  blocks. The engine owns its own thread.
 */
#ifndef SMARTMIC_PHONE_API_H
#define SMARTMIC_PHONE_API_H

#include <stdint.h>

#if defined(_WIN32)
#define SM_API __declspec(dllexport)
#else
#define SM_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sm_phone sm_phone;

/* Connection state, mirroring session::SessionState. */
enum {
    SM_CONN_IDLE = 0,
    SM_CONN_PAIRING = 1,
    SM_CONN_AUTHENTICATED = 2,
    SM_CONN_FAILED = 3
};

/* PTT state, mirroring docs/architecture/06-mobile-state-model.md.
 * LIVE is only ever entered on the PC's PTT_READY. */
enum {
    SM_PTT_READY = 0,
    SM_PTT_CONNECTING = 1,
    SM_PTT_LIVE = 2,
    SM_PTT_RELEASING = 3,
    SM_PTT_FAILED = 4
};

/* Haptic hints the UI should fire, consumed one at a time. */
enum {
    SM_HAPTIC_NONE = 0,
    SM_HAPTIC_WENT_LIVE = 1,
    SM_HAPTIC_RETURNED = 2,
    SM_HAPTIC_FAILED = 3
};

/*  Creates the engine and starts pairing. `identity_dir` is a writable
 *  directory; the device's long-term key is created there on first run and
 *  reused afterwards, so a phone keeps its identity across app restarts.
 *  Returns NULL only on allocation failure or bad arguments. */
SM_API sm_phone* sm_phone_create(const char* host,
                                 uint16_t port,
                                 const char* session_id,
                                 const char* code,
                                 const char* identity_dir);

SM_API void sm_phone_destroy(sm_phone* p);

SM_API int   sm_phone_conn_state(sm_phone* p);
SM_API int   sm_phone_ptt_state(sm_phone* p);
SM_API int   sm_phone_rtt_ms(sm_phone* p);
SM_API float sm_phone_mic_level(sm_phone* p);      /* 0..1, for the meter */
SM_API int   sm_phone_take_haptic(sm_phone* p);    /* returns and clears */

/* Finger down / finger up. Both are idempotent and safe to call in any state;
 * release in particular must never be a no-op that leaves the mic open. */
SM_API void sm_phone_press(sm_phone* p);
SM_API void sm_phone_release(sm_phone* p);

/* Routing mode on the PC: 0 Auto, 1 PhoneOnly, 2 LocalOnly, 3 Mute. */
SM_API void sm_phone_set_mode(sm_phone* p, int mode);

/* Buffers are owned by the engine and valid until the next call on the same
 * handle; Dart copies them immediately. */
/*  Transport counters, which answer the only question that matters when
 *  pairing hangs: is this phone actually putting packets on the wire?
 *
 *    sent > 0, failures == 0, received == 0  -> the phone is transmitting and
 *                                               the packets die en route
 *    failures > 0                            -> the OS is refusing the send:
 *                                               routing, VPN or permission
 *    sent == 0                               -> the app never got as far as
 *                                               transmitting
 *
 *  Any argument may be NULL. */
SM_API void sm_phone_net_stats(sm_phone* p,
                               uint64_t* out_sent,
                               uint64_t* out_send_failures,
                               uint64_t* out_received);

SM_API const char* sm_phone_peer_fingerprint(sm_phone* p);
SM_API const char* sm_phone_device_id(sm_phone* p);
SM_API const char* sm_phone_last_error(sm_phone* p);
SM_API const char* sm_phone_pc_status(sm_phone* p);   /* JSON from STATUS */

/*  Parses smartmic://pair?... so the QR scanner does not have to.
 *  Returns 1 on success. Output buffers must be at least 128 bytes. */
SM_API int sm_phone_parse_uri(const char* uri,
                              char* out_host, int host_len,
                              uint16_t* out_port,
                              char* out_session, int session_len,
                              char* out_code, int code_len,
                              char* out_fingerprint, int fp_len);

/* Version of this native core, for the about screen and bug reports. */
SM_API const char* sm_phone_version(void);

#ifdef __cplusplus
}
#endif

#endif /* SMARTMIC_PHONE_API_H */
