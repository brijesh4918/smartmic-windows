<#
    Creates the root-enumerated device node for SmartMic and binds the driver
    to it.

    This is the job devcon.exe does. devcon ships with the WDK, is not on PATH
    on a clean machine, and -- as we found the hard way -- is not always where
    the build script expects it, in which case install-driver.bat reported
    success while creating nothing at all.

    There is no built-in Windows command for this, so the three SetupAPI calls
    devcon makes are called directly here. No WDK, no bundled binary.
#>
param(
    [Parameter(Mandatory = $true)][string]$InfPath,
    [string]$HardwareId = 'root\smartmic'
)

$ErrorActionPreference = 'Stop'
$InfPath = (Resolve-Path -LiteralPath $InfPath).Path

Add-Type -Namespace SmartMic -Name Setup -UsingNamespace System.Runtime.InteropServices -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)]
public struct SP_DEVINFO_DATA {
    public int    cbSize;
    public Guid   ClassGuid;
    public int    DevInst;
    public IntPtr Reserved;
}

[DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern IntPtr SetupDiCreateDeviceInfoList(ref Guid ClassGuid, IntPtr hwndParent);

[DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern bool SetupDiCreateDeviceInfoW(
    IntPtr DeviceInfoSet, string DeviceName, ref Guid ClassGuid,
    string DeviceDescription, IntPtr hwndParent, int CreationFlags,
    ref SP_DEVINFO_DATA DeviceInfoData);

[DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern bool SetupDiSetDeviceRegistryPropertyW(
    IntPtr DeviceInfoSet, ref SP_DEVINFO_DATA DeviceInfoData,
    int Property, byte[] PropertyBuffer, int PropertyBufferSize);

[DllImport("setupapi.dll", SetLastError = true)]
public static extern bool SetupDiCallClassInstaller(
    int InstallFunction, IntPtr DeviceInfoSet, ref SP_DEVINFO_DATA DeviceInfoData);

[DllImport("setupapi.dll", SetLastError = true)]
public static extern bool SetupDiDestroyDeviceInfoList(IntPtr DeviceInfoSet);

[DllImport("newdev.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern bool UpdateDriverForPlugAndPlayDevicesW(
    IntPtr hwndParent, string HardwareId, string FullInfPath,
    int InstallFlags, out bool bRebootRequired);
'@

# Must match ClassGuid in smartmic.inf (Class = MEDIA).
$mediaClass       = [Guid]'4d36e96c-e325-11ce-bfc1-08002be10318'
$DICD_GENERATE_ID = 0x00000001
$SPDRP_HARDWAREID = 0x00000001
$DIF_REGISTERDEVICE = 0x00000019
$INSTALLFLAG_FORCE  = 0x00000001

function Invoke-UpdateDriver {
    $reboot = $false
    $ok = [SmartMic.Setup]::UpdateDriverForPlugAndPlayDevicesW(
              [IntPtr]::Zero, $HardwareId, $InfPath, $INSTALLFLAG_FORCE, [ref]$reboot)
    return @{ Ok = $ok; Err = [Runtime.InteropServices.Marshal]::GetLastWin32Error(); Reboot = $reboot }
}

# Try binding to an existing node first. Creating unconditionally would leave a
# second, duplicate device behind on every re-run.
Write-Host "    binding $HardwareId to $InfPath"
$r = Invoke-UpdateDriver
if ($r.Ok) {
    Write-Host "    bound to the existing device node"
    if ($r.Reboot) { Write-Host "    (a reboot is required)" }
    exit 0
}

# 0xE000020B = ERROR_NO_SUCH_DEVINST: nothing to bind to yet, so make one.
if ($r.Err -ne 0xE000020B -and $r.Err -ne 0x800F020B) {
    Write-Host "    no existing node (error 0x$('{0:X}' -f $r.Err)); creating one"
}

$devInfo = [SmartMic.Setup]::SetupDiCreateDeviceInfoList([ref]$mediaClass, [IntPtr]::Zero)
if ($devInfo -eq [IntPtr]::Zero -or $devInfo -eq [IntPtr](-1)) {
    throw "SetupDiCreateDeviceInfoList failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}

try {
    $data = New-Object SmartMic.Setup+SP_DEVINFO_DATA
    $data.cbSize = [Runtime.InteropServices.Marshal]::SizeOf([type]'SmartMic.Setup+SP_DEVINFO_DATA')

    if (-not [SmartMic.Setup]::SetupDiCreateDeviceInfoW(
            $devInfo, $HardwareId, [ref]$mediaClass, $null, [IntPtr]::Zero,
            $DICD_GENERATE_ID, [ref]$data)) {
        throw "SetupDiCreateDeviceInfo failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }

    # SPDRP_HARDWAREID is REG_MULTI_SZ: the string, then two terminators.
    $bytes = [Text.Encoding]::Unicode.GetBytes($HardwareId + "`0`0")
    if (-not [SmartMic.Setup]::SetupDiSetDeviceRegistryPropertyW(
            $devInfo, [ref]$data, $SPDRP_HARDWAREID, $bytes, $bytes.Length)) {
        throw "SetupDiSetDeviceRegistryProperty failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }

    if (-not [SmartMic.Setup]::SetupDiCallClassInstaller($DIF_REGISTERDEVICE, $devInfo, [ref]$data)) {
        throw "DIF_REGISTERDEVICE failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    Write-Host "    device node created"
}
finally {
    [void][SmartMic.Setup]::SetupDiDestroyDeviceInfoList($devInfo)
}

$r = Invoke-UpdateDriver
if (-not $r.Ok) {
    throw "the node was created but the driver would not bind to it: 0x$('{0:X}' -f $r.Err)"
}
Write-Host "    driver bound to the new device node"
if ($r.Reboot) { Write-Host "    (a reboot is required)" }
exit 0
