<#
.SYNOPSIS
  Send command(s) to the BWM427 board over its USB virtual COM port
  (USB CDC, VID_0483 PID_5740) and print the reply.

.DESCRIPTION
  Finds the board's COM port by USB VID/PID (or uses -Port), opens it
  (115200 8N1, DTR on), sends each command, reads the reply and prints it.
  A reply normally ends with a line "OK..." or "ERR..."; reading stops there,
  or after -Seconds. For "stream N" the script reads for the full -Seconds
  and then sends "stream 0".
  Everything is also appended to usb_diag.log next to this script.
  Non-interactive; works in Windows PowerShell 5.1, also as SYSTEM (e.g.
  started by a remote-management agent).

  Board commands: help, ver, diag, stream N, set ..., boot|dfu [force],
  reset [force]

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File usb_diag.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File usb_diag.ps1 -Command ver,diag
  powershell -NoProfile -ExecutionPolicy Bypass -File usb_diag.ps1 -Command "stream 100" -Seconds 10

  Exit codes: 0 ok, 1 board not found / port error, 2 a command answered ERR
#>
param(
    [string[]]$Command = @('diag'),
    [double]$Seconds = 3,
    [string]$Port = '',
    [string]$LogFile = ''
)

$ErrorActionPreference = 'Continue'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $LogFile) { $LogFile = Join-Path $ScriptDir 'usb_diag.log' }

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

function Find-BoardPort {
    $devs = Get-UsbDevices $CDC_ID
    $ports = @()
    foreach ($d in $devs) {
        $com = Get-ComName $d
        Log ("found {0} [{1}] status={2} port={3}" -f $d.Name, $d.PNPDeviceID, $d.Status, $com)
        if ($com) { $ports += $com }
    }
    if ($ports.Count -gt 1) { Log "WARNING: several boards found, using $($ports[0]) (use -Port to choose)" }
    if ($ports.Count -ge 1) { return $ports[0] }
    return $null
}

function Open-Board([string]$name) {
    $sp = New-Object System.IO.Ports.SerialPort $name, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $sp.DtrEnable = $true          # firmware: DTR 1->0 on close stops "stream"
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

# Read lines for up to $seconds; with $untilEnd stop after a final reply line
function Read-Reply($sp, [double]$seconds, [bool]$untilEnd) {
    $deadline = (Get-Date).AddSeconds($seconds)
    $buf = ''
    $lines = New-Object System.Collections.Generic.List[string]
    while ((Get-Date) -lt $deadline) {
        $chunk = ''
        try { $chunk = $sp.ReadExisting() } catch { Log "read: $($_.Exception.Message)"; break }
        if ($chunk.Length -eq 0) { Start-Sleep -Milliseconds 30; continue }
        $buf += $chunk
        while (($i = $buf.IndexOf("`n")) -ge 0) {
            $line = $buf.Substring(0, $i).TrimEnd("`r")
            $buf = $buf.Substring($i + 1)
            Log "< $line"
            $lines.Add($line)
            if ($untilEnd -and ($line -match '^(OK|ERR|DFU\.\.\.|RESET\.\.\.)')) { return ,$lines }
        }
    }
    if ($buf.Length -gt 0) { Log "< $buf"; $lines.Add($buf) }
    return ,$lines
}

# ---------------------------------------------------------------------------
Log "=== usb_diag $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') commands: $($Command -join ' | ')"

if (-not $Port) {
    $Port = Find-BoardPort
    if (-not $Port) {
        if ((Get-UsbDevices $DFU_ID).Count -gt 0) {
            Log "ERROR: board is in DFU bootloader mode (0483:DF11), no COM port. Flash it (usb_flash.ps1) or power-cycle."
        } else {
            Log "ERROR: no USB device $CDC_ID found (cable? firmware without USB CDC?)"
        }
        exit 1
    }
}

$sp = $null
$rc = 0
try {
    $sp = Open-Board $Port
    Log "opened $Port"
    foreach ($cmd in $Command) {
        $c = $cmd.Trim()
        if (-not $c) { continue }
        $isStream = $c -match '^\s*stream\s+[1-9]'
        Log "> $c"
        $sp.Write("$c`r`n")
        $reply = Read-Reply $sp $Seconds (-not $isStream)
        if (@($reply | Where-Object { $_ -match '^ERR' }).Count -gt 0) { $rc = 2 }
        if ($reply.Count -eq 0) { Log "(no reply within $Seconds s)" }
        if ($isStream) {
            Log "> stream 0"
            $sp.Write("stream 0`r`n")
            $null = Read-Reply $sp 1 $true
        }
        if (@($reply | Where-Object { $_ -match '^(DFU|RESET)\.\.\.' }).Count -gt 0) {
            Log "board is rebooting, closing the port"
            break
        }
    }
} catch {
    Log "ERROR: $Port : $($_.Exception.Message)"
    $rc = 1
} finally {
    Close-Board $sp
}
Log "=== done, exit $rc"
exit $rc
