<#
.SYNOPSIS
  Re-flash the BWM427 board over USB without touching BOOT0/NRST.

.DESCRIPTION
  1. Checks the firmware image, the flashing script and dfu-util exist.
  2. Finds the board's virtual COM port (USB CDC, VID_0483 PID_5740), opens it
     (115200, DTR on), sends "boot" ("boot force" with -Force), closes it.
     The firmware answers "DFU...", detaches from USB and restarts into the
     STM32 system bootloader (USB DFU, VID_0483 PID_DF11).
     While recording to SD the firmware refuses plain "boot" (reply "ERR ...");
     use -Force to interrupt the recording.
  3. Waits up to -DfuWaitSec for the DFU device to appear.
     If the board is already in DFU mode, steps 2-3 are skipped.
  4. Runs dfu_flash.cmd with the image (environment variable BWM427_IMAGE):
     write + read-back verify + leave, and prints its log (flash.log next to
     dfu_flash.cmd).
  5. Waits for the board to come back as a COM port and prints "ver".
  Everything is also appended to usb_flash.log next to this script.
  Non-interactive; Windows PowerShell 5.1, works as SYSTEM (e.g. started by a
  remote-management agent).

  Defaults: the image bwm427_new.bin and dfu_flash.cmd next to this script.
  dfu-util.exe: -DfuUtil, else the DFU_UTIL environment variable, else
  dfu-util.exe on PATH, else the copy the Arduino IDE installs under
  AppData\Local\Arduino15 (current user first, then any user). The path found
  is handed to dfu_flash.cmd in DFU_UTIL.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File .\usb_flash.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File .\usb_flash.ps1 -Force
  powershell -NoProfile -ExecutionPolicy Bypass -File .\usb_flash.ps1 -Firmware ..\..\BWM427_Inclinometer\build\BWM427_Inclinometer.bin -DfuUtil C:\Tools\dfu-util\dfu-util.exe

  Exit codes: 0 flashed and verified, 1 board not found / port error,
  2 board refused "boot" (ERR, e.g. recording), 3 DFU device did not appear,
  4 flashing or verify failed, 5 flashed OK but the new firmware did not
  answer on USB, 6 firmware image, dfu_flash.cmd or dfu-util missing/invalid
#>
param(
    [string]$Port = '',
    [switch]$Force,
    [int]$DfuWaitSec = 10,
    [int]$BackWaitSec = 20,
    [string]$Firmware = (Join-Path $PSScriptRoot 'bwm427_new.bin'),
    [string]$FlashCmd = (Join-Path $PSScriptRoot 'dfu_flash.cmd'),
    [string]$FlashLog = '',
    [string]$DfuUtil = '',
    [switch]$SkipVer,
    [string]$LogFile = ''
)

$ErrorActionPreference = 'Continue'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $LogFile) { $LogFile = Join-Path $ScriptDir 'usb_flash.log' }

$CDC_ID = 'VID_0483&PID_5740'
$DFU_ID = 'VID_0483&PID_DF11'

function Log([string]$msg) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $msg
    [Console]::WriteLine($msg)
    try { Add-Content -Path $LogFile -Value $line -Encoding ASCII } catch { }
}

# Present USB devices whose PNPDeviceID contains $id (e.g. VID_0483&PID_5740)
function Get-UsbDevices([string]$id) {
    $all = @(Get-CimInstance -ClassName Win32_PnPEntity -Filter "PNPDeviceID LIKE '%VID_0483%'" -ErrorAction SilentlyContinue)
    return @($all | Where-Object { $_.PNPDeviceID -like "USB\$id*" })
}

# COM port name of a PnP device: from "Name (COMx)" or from the registry
function Get-ComName($dev) {
    if ($dev.Name -match '\((COM\d+)\)') { return $Matches[1] }
    $key = 'HKLM:\SYSTEM\CurrentControlSet\Enum\' + $dev.PNPDeviceID + '\Device Parameters'
    try {
        $pn = (Get-ItemProperty -Path $key -Name PortName -ErrorAction Stop).PortName
        if ($pn) { return $pn }
    } catch { }
    return $null
}

function Find-BoardPort([bool]$quiet) {
    $devs = Get-UsbDevices $CDC_ID
    $ports = @()
    foreach ($d in $devs) {
        $com = Get-ComName $d
        if (-not $quiet) { Log ("found {0} [{1}] status={2} port={3}" -f $d.Name, $d.PNPDeviceID, $d.Status, $com) }
        if ($com) { $ports += $com }
    }
    if ($ports.Count -gt 1 -and -not $quiet) { Log "WARNING: several boards found, using $($ports[0]) (use -Port to choose)" }
    if ($ports.Count -ge 1) { return $ports[0] }
    return $null
}

function Open-Board([string]$name) {
    $sp = New-Object System.IO.Ports.SerialPort $name, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $sp.DtrEnable = $true
    $sp.RtsEnable = $true
    $sp.ReadTimeout = 200
    $sp.WriteTimeout = 2000
    $sp.NewLine = "`r`n"
    $sp.Encoding = [System.Text.Encoding]::ASCII
    $sp.Open()
    Start-Sleep -Milliseconds 100
    $sp.DiscardInBuffer()
    return $sp
}

function Close-Board($sp) {
    if ($null -eq $sp) { return }
    try { if ($sp.IsOpen) { $sp.Close() } } catch { Log "close: $($_.Exception.Message)" }
    try { $sp.Dispose() } catch { }
}

# Read lines for up to $seconds; stop after a final reply line
function Read-Reply($sp, [double]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    $buf = ''
    $lines = New-Object System.Collections.Generic.List[string]
    while ((Get-Date) -lt $deadline) {
        $chunk = ''
        try { $chunk = $sp.ReadExisting() } catch { Log "read: $($_.Exception.Message)"; break }
        if ($chunk.Length -eq 0) { Start-Sleep -Milliseconds 20; continue }
        $buf += $chunk
        while (($i = $buf.IndexOf("`n")) -ge 0) {
            $line = $buf.Substring(0, $i).TrimEnd("`r")
            $buf = $buf.Substring($i + 1)
            Log "< $line"
            $lines.Add($line)
            if ($line -match '^(OK|ERR|DFU\.\.\.|RESET\.\.\.)') { return ,$lines }
        }
    }
    if ($buf.Length -gt 0) { Log "< $buf"; $lines.Add($buf) }
    return ,$lines
}

# Full path of dfu-util.exe or $null. Same search order as dfu_flash.cmd:
# -DfuUtil, $env:DFU_UTIL, PATH, Arduino15 of the current user, of any user
function Resolve-DfuUtil([string]$path) {
    if ($path) {
        if (Test-Path -LiteralPath $path -PathType Leaf) { return (Resolve-Path -LiteralPath $path).Path }
        return $null
    }
    if ($env:DFU_UTIL -and (Test-Path -LiteralPath $env:DFU_UTIL -PathType Leaf)) { return $env:DFU_UTIL }
    $onPath = @(Get-Command dfu-util.exe -CommandType Application -ErrorAction SilentlyContinue)
    if ($onPath.Count -gt 0) { return $onPath[0].Path }
    $tail = 'AppData\Local\Arduino15\packages\arduino\tools\dfu-util\*\dfu-util.exe'
    $patterns = @()
    if ($env:LOCALAPPDATA) { $patterns += Join-Path $env:LOCALAPPDATA 'Arduino15\packages\arduino\tools\dfu-util\*\dfu-util.exe' }
    $patterns += Join-Path $env:SystemDrive ('Users\*\' + $tail)
    foreach ($p in $patterns) {
        $found = @(Get-ChildItem -Path $p -ErrorAction SilentlyContinue | Sort-Object FullName)
        if ($found.Count -gt 0) { return $found[-1].FullName }
    }
    return $null
}

function Wait-Device([string]$id, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $d = @(Get-UsbDevices $id)
        if ($d.Count -gt 0) { return $d[0] }
        Start-Sleep -Milliseconds 500
    }
    return $null
}

# ---------------------------------------------------------------------------
Log "=== usb_flash $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') (user $env:USERNAME, force=$Force)"

# 1. Pre-flight: never send the board to DFU without something to flash
if (-not (Test-Path -LiteralPath $FlashCmd)) { Log "ERROR: $FlashCmd not found"; exit 6 }
$FlashCmd = (Resolve-Path -LiteralPath $FlashCmd).Path
# dfu_flash.cmd writes flash.log next to itself
if (-not $FlashLog) { $FlashLog = Join-Path (Split-Path -Parent $FlashCmd) 'flash.log' }
if (-not (Test-Path -LiteralPath $Firmware)) { Log "ERROR: firmware $Firmware not found"; exit 6 }
$fw = Get-Item -LiteralPath $Firmware
$hdr = [System.IO.File]::ReadAllBytes($fw.FullName)
$sp0 = 0
if ($hdr.Length -ge 8) { $sp0 = [BitConverter]::ToUInt32($hdr, 0) }
if ($fw.Length -lt 1024 -or $fw.Length -gt 512KB -or ($sp0 -band 0xFFF00000) -ne 0x20000000) {
    Log ("ERROR: {0} does not look like an STM32F411 image (size {1}, initial SP 0x{2:X8})" -f $fw.FullName, $fw.Length, $sp0)
    exit 6
}
$hash = (Get-FileHash -LiteralPath $fw.FullName -Algorithm SHA256).Hash
Log ("firmware {0}: {1} bytes, {2}, SHA256 {3}" -f $fw.FullName, $fw.Length, $fw.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'), $hash)
$dfuExe = Resolve-DfuUtil $DfuUtil
if (-not $dfuExe) {
    Log "ERROR: dfu-util.exe not found (use -DfuUtil, set DFU_UTIL or put it on PATH)"
    exit 6
}
$env:DFU_UTIL = $dfuExe   # dfu_flash.cmd uses the same dfu-util
Log "dfu-util $dfuExe"

# 2-3. Board -> DFU
$dfu = @(Get-UsbDevices $DFU_ID)
if ($dfu.Count -gt 0) {
    Log ("board already in DFU mode: {0} [{1}] status={2}" -f $dfu[0].Name, $dfu[0].PNPDeviceID, $dfu[0].Status)
} else {
    if (-not $Port) { $Port = Find-BoardPort $false }
    if (-not $Port) {
        Log "ERROR: no USB device $CDC_ID (board COM port) and no $DFU_ID (DFU) found"
        $other = @(Get-CimInstance -ClassName Win32_PnPEntity -Filter "PNPDeviceID LIKE '%VID_0483%'" -ErrorAction SilentlyContinue)
        foreach ($o in $other) { Log ("  seen: {0} [{1}] status={2}" -f $o.Name, $o.PNPDeviceID, $o.Status) }
        exit 1
    }
    $cmd = 'boot'
    if ($Force) { $cmd = 'boot force' }
    $sp = $null
    $reply = $null
    try {
        $sp = Open-Board $Port
        Log "opened $Port, > $cmd"
        $sp.Write("$cmd`r`n")
        $reply = Read-Reply $sp 3
    } catch {
        Log "ERROR: $Port : $($_.Exception.Message)"
        Close-Board $sp
        exit 1
    }
    # Close at once: the firmware waits ~0.2 s after the reply before it
    # detaches, so the port is closed before the device disappears
    Close-Board $sp
    if (@($reply | Where-Object { $_ -match '^ERR' }).Count -gt 0) {
        Log "ERROR: board refused '$cmd' (see reply above). If it is recording to SD, stop it or rerun with -Force."
        exit 2
    }
    if (@($reply | Where-Object { $_ -match '^DFU\.\.\.' }).Count -eq 0) {
        Log "WARNING: no 'DFU...' reply, waiting for the DFU device anyway"
    }
    Log "waiting up to $DfuWaitSec s for $DFU_ID ..."
    $d = Wait-Device $DFU_ID $DfuWaitSec
    if ($null -eq $d) {
        Log "ERROR: DFU device $DFU_ID did not appear within $DfuWaitSec s"
        $still = @(Get-UsbDevices $CDC_ID)
        if ($still.Count -gt 0) { Log "  the board is still a COM port (command not received or firmware without 'boot')" }
        exit 3
    }
    Log ("DFU device: {0} [{1}] status={2}" -f $d.Name, $d.PNPDeviceID, $d.Status)
    Start-Sleep -Milliseconds 1000   # let the driver settle before dfu-util
}

# 4. Flash: dfu_flash.cmd writes, reads back, compares and leaves DFU
Log "running $FlashCmd ..."
$t0 = Get-Date
# The image goes in an environment variable: quoting a second path argument
# through cmd /c differs between PowerShell versions
$env:BWM427_IMAGE = $fw.FullName
& cmd.exe /c "`"$FlashCmd`"" 2>&1 | ForEach-Object { Log "  $_" }
Log ("dfu_flash.cmd finished in {0:N1} s, log {1}:" -f ((Get-Date) - $t0).TotalSeconds, $FlashLog)
$flashText = ''
if (Test-Path -LiteralPath $FlashLog) {
    $flashText = Get-Content -LiteralPath $FlashLog -Raw
    foreach ($l in ($flashText -split "`r?`n")) { if ($l) { Log "  | $l" } }
} else {
    Log "ERROR: $FlashLog not found"
}
if ($flashText -notmatch 'VERIFY OK') {
    Log "ERROR: flashing/verify failed (no 'VERIFY OK'). The board stays in DFU mode; rerun this script to retry."
    exit 4
}
Log "VERIFY OK"

# 5. New firmware back on USB?
if ($SkipVer) { Log "=== done, exit 0"; exit 0 }
Log "waiting up to $BackWaitSec s for the board COM port ($CDC_ID) ..."
$back = Wait-Device $CDC_ID $BackWaitSec
$newPort = $null
if ($null -ne $back) { $newPort = Get-ComName $back }
if (-not $newPort) {
    Log "WARNING: flashed and verified, but the board did not come back as a COM port within $BackWaitSec s"
    exit 5
}
Start-Sleep -Milliseconds 1500       # firmware init (display, SD) before the first command
$sp = $null
$rc = 5
try {
    $sp = Open-Board $newPort
    Log "opened $newPort, > ver"
    $sp.Write("ver`r`n")
    $reply = Read-Reply $sp 3
    if (@($reply | Where-Object { $_ -match '^OK' }).Count -gt 0) { $rc = 0 }
    else { Log "WARNING: no answer to 'ver'" }
} catch {
    Log "WARNING: $newPort : $($_.Exception.Message)"
} finally {
    Close-Board $sp
}
Log "=== done, exit $rc"
exit $rc
