# 8. Repository Tree

```
smartmic/
├── apps/
│   ├── mobile/                     # Flutter shell + native audio/net plugins
│   │   ├── lib/{ui,state,protocol}/
│   │   ├── android/  (Kotlin: AudioRecord, Keystore, NsdManager, WebRTC)
│   │   └── ios/      (Swift: AVAudioEngine, Keychain, NWBrowser, WebRTC)
│   └── windows-desktop/            # WinUI 3 / C#
├── windows/
│   ├── driver/                     # Phase 5. SysVAD-derived WaveRT capture
│   ├── audio-service/              # ★ Phase 1 lives here
│   │   ├── include/smartmic/       # public headers of the portable core
│   │   ├── src/                    # portable core implementation
│   │   ├── platform/
│   │   │   ├── windows/            # WASAPI capture/render, MMDevice notify
│   │   │   └── host/               # tone/WAV sources + WAV sink (dev + CI)
│   │   ├── app/                    # smartmic-router CLI harness
│   │   └── tests/                  # unit tests
│   └── installer/                  # WiX
├── shared/
│   ├── protocol/                   # smartmic.proto-equivalent: one JSON schema
│   ├── schemas/
│   └── test-vectors/
├── backend/                        # Phase 9 only; not linked by the LAN product
│   ├── signaling/
│   └── turn-config/
├── docs/
│   ├── architecture/               # 01..12 (this set)
│   ├── adr/                        # ADR-001..010
│   ├── security/  driver/  protocol/  testing/  release/
└── tests/
    ├── integration/  e2e/  audio-fixtures/  network-chaos/
```

Rule: `backend/` must never appear in a build dependency of `windows/` or
`apps/`. The LAN product has to be shippable with the cloud deleted.
