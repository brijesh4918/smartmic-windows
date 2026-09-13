# EV Certificate: what I need from you once it arrives

You asked what I need after the EV certification. Here it is, concretely.

## What I never need

**The certificate itself, its private key, its password, or the hardware
token.** I do not want them and should not have them. An EV code-signing key
lives on a hardware token in your possession; the whole security value of it is
that it cannot be copied off that token. Signing is something *you* run, on your
machine, with the token plugged in.

So: nothing secret ever needs to reach me.

## What I do need — four plain facts

1. **The exact subject name on the certificate.**
   Something like `CN=Your Company Ltd, O=Your Company Ltd, L=City, C=IN`.
   It goes into the signing command verbatim.

2. **The certificate's SHA-1 thumbprint.**
   Find it: Windows → `certmgr.msc` → Personal → Certificates → double-click
   yours → Details → Thumbprint. It is a 40-character hex string.
   I use this so the build script picks the right certificate when several are
   installed, instead of guessing.

3. **Your Partner Center / Hardware Developer account status.**
   Specifically: is the account created, and is the EV certificate registered
   against it? A certificate without the account cannot submit a driver, and
   the account has to be verified with the same legal identity as the
   certificate. This is the step that takes weeks, which is why I asked you to
   start early.

4. **The Seller ID (PublisherID)** that Partner Center shows once the account
   is verified. It appears in the submission metadata.

## What changes in the code

Nothing in the driver source. Only `build-driver.bat` changes: the `makecert`
block disappears and the `signtool` lines point at your real certificate:

```
signtool sign /v /fd sha256 /sha1 <THUMBPRINT> /tr http://timestamp.digicert.com /td sha256 smartmic.cat
```

I will write that version once you send me items 1 and 2.

## The order things have to happen in

```
  EV certificate issued
          |
          v
  Partner Center account created and verified with the SAME legal identity
          |
          v
  Certificate registered to the account
          |
          v
  Driver built and signed with the EV cert   <-- you run this
          |
          v
  .cab submitted to the Hardware Developer Center for attestation signing
          |
          v
  Microsoft returns a Microsoft-signed package
          |
          v
  That package installs on any Windows machine, no test signing, no watermark
```

Attestation signing (the simpler path) is enough for a driver like this one and
does **not** require HLK test runs. Full WHCP certification does, and would only
matter if you later want the driver distributed through Windows Update.

## One thing worth deciding early

The certificate's legal identity becomes the publisher name every user sees in
the Windows install prompt and in Device Manager. Whatever name you register
is the name your users will associate with the driver, and changing it later
means a new certificate. Worth a minute's thought before you buy.
