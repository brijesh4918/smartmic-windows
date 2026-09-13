# Run this on the Mac before anything else

While I was building, macOS's Gatekeeper daemon got stuck:

```
$ ps aux | grep syspolicyd
root  547  97.6%  /usr/libexec/syspolicyd
```

At 100% CPU it stops answering, and **every newly compiled program hangs before
it can start** — no error, no output, it just sits there. Programs built before
it wedged keep working fine, which is what made it confusing.

This blocks the Mac build, the iPhone build, and anything else that compiles.

## The fix

```bash
sudo killall -9 syspolicyd
```

macOS restarts it immediately; nothing is lost. Rebooting does the same thing.

## Check it worked

```bash
ps aux | grep syspolicyd
```

The number in the third column should be close to `0.0`, not `97`.

Then a quick sanity check that new programs run again:

```bash
printf '#include <cstdio>\nint main(){puts("ok");}\n' > /tmp/t.cpp
clang++ /tmp/t.cpp -o /tmp/t && /tmp/t
```

If that prints `ok`, you are clear to continue with `START-HERE.md`.

## Why it happened

Most likely the Xcode component install plus several build processes being
killed at once while they were waiting on code-signing checks. It is a known
macOS failure mode, not something SmartMic caused, and it does not indicate
anything wrong with the project.
