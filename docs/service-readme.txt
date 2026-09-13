SmartMic service
================

First run this, before anything else:

    smartmic-service.exe --diagnose

It reports three things separately -- whether the driver package is installed,
whether Windows actually loaded the driver, and whether its control interface
is available -- and tells you which one is the problem. Those three failures
look identical from the outside and have completely different fixes.

Normal use (needs the driver installed and loaded):

    smartmic-service.exe --port 47820

Test WITHOUT the driver. This pairs a phone and records the result to a WAV
file, so you can prove the phone half works before fighting the driver:

    smartmic-service.exe --port 47820 --sink wav --out C:\Users\Public\test.wav

Options
-------
    --sink auto     driver if present, otherwise a WAV file   (default)
    --sink driver   driver only; fail with a diagnosis if missing
    --sink wav      always a WAV file
    --sink cable    a third-party virtual cable, device id in --out
    --port <n>      UDP port (default 47820)
    --duration <s>  stop after this many seconds
    --diagnose      report driver state and exit
    --help

If something goes wrong
-----------------------
Send the output of:

    smartmic-service.exe --diagnose

and, if the driver is installed but not loading, the bottom of:

    %SystemRoot%\INF\setupapi.dev.log
