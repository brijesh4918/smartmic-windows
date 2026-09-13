<#
    Creates (or reuses) the SmartMic development code-signing certificate and
    exports the public half next to the driver.

    This lives in its own file rather than inline in the .bat on purpose:
    multi-line PowerShell spliced into batch with caret continuations is the
    single most fragile construct in a Windows build script, and it is not
    worth debugging through two layers of quoting.

    Development only. Production signing uses the EV certificate and Microsoft
    attestation -- see docs/adr/ADR-010-driver-signing-and-release.md.
#>
param(
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$Subject = 'CN=SmartMicTestCert'
)

$ErrorActionPreference = 'Stop'

$cert = Get-ChildItem -Path Cert:\LocalMachine\My |
        Where-Object { $_.Subject -eq $Subject } |
        Select-Object -First 1

if ($null -eq $cert) {
    Write-Host "    creating $Subject"
    $cert = New-SelfSignedCertificate `
        -Subject $Subject `
        -CertStoreLocation 'Cert:\LocalMachine\My' `
        -Type CodeSigningCert `
        -NotAfter (Get-Date).AddYears(5)
} else {
    Write-Host "    reusing existing $Subject"
}

if (-not (Test-Path $OutDir)) {
    throw "output folder does not exist: $OutDir"
}

$cerPath = Join-Path $OutDir 'SmartMicTestCert.cer'
Export-Certificate -Cert $cert -FilePath $cerPath -Force | Out-Null

Write-Host "    thumbprint: $($cert.Thumbprint)"
Write-Host "    exported  : $cerPath"
