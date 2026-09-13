# 12. Known Technical Risks

| # | Risk | Likelihood | Impact | Mitigation / early signal |
|---|---|---|---|---|
| R1 | **Driver signing is the long pole.** EV cert + Partner Center + attestation/HLK can take weeks of calendar time and is gated on a real company identity. | High | Ship-blocking | Start the EV cert + Partner Center account in Phase 0, in parallel with all engineering. Treat it as procurement, not engineering. |
| R2 | Apps cache the endpoint format or hold exclusive mode, so a format change mid-call breaks them | Medium | High | Fix the endpoint format at 48 k mono for its entire life; never renegotiate. Verified by M1/M3. |
| R3 | WaveRT clock drift between the driver's timeline and the service's producer rate causes slow underrun | Medium | High | Service is driven by the *sink's* clock, not a wall timer; ring fill level is a feedback signal into an adaptive jitter target. Instrument `underrunCount` from day one. |
| R4 | Crossfade is inaudible in the lab but audible with AGC/noise-suppression in Teams reacting to the level step | Medium | Medium | Conservative per-source gain normalisation; measure with Teams' processing on, not just on raw WAV. |
| R5 | iOS background/lock restrictions make PTT unreliable when the screen is off, and App Review may reject a mic-in-background story | Medium | High for iOS | Design for foreground-only PTT first; evaluate VoIP/`playAndRecord` categories explicitly in Phase 4 rather than assuming. |
| R6 | Android OEM battery managers kill the foreground service mid-PTT | High | Medium | `microphone` foreground service type, heartbeat watchdog on the PC makes the failure safe; surface "your phone killed us" in the UI. |
| R7 | Enterprise Wi-Fi blocks mDNS and/or client isolation blocks peer-to-peer entirely | High in offices | High | QR carries direct host/port; client isolation has no LAN workaround — that is the honest case for Phase 9 cloud relay. Detect and say so clearly instead of retrying forever. |
| R8 | Bluetooth headset selected as local source forces HFP and wrecks call quality | Medium | Medium | Prefer the headset's mic only when the user opts in; never auto-switch to a Bluetooth mic mid-call. |
| R9 | WebRTC as a native dependency on three platforms is heavy to build and update | Medium | Medium | Pin a known-good build; isolate behind `ITransport` so it is replaceable. |
| R10 | Antivirus/EDR flags a signed kernel audio driver that streams from the network — even though the driver does no networking | Low–Medium | High | The strict kernel/user split is itself the mitigation and the defensible story; budget time for vendor whitelisting. |
| R11 | Two SmartMic services (upgrade, or a second user session) both claim the ring | Low | High | Single-client claim in the driver via interlocked exchange; second claimant fails cleanly. |
| R12 | Windows picks SmartMic as default *before* the service records the previous default, creating the recursion the design forbids | Low | High | Installer records `preferredLocalInputId` **before** driver install; registry filter + router guard are the belt and braces. Test A12. |
