<#
.SYNOPSIS
  RS485 bus timing sweep for the BWM427 board: measure the poll rate, sensor
  reply latency and errors for several bus gaps (firmware "set gap N").

.DESCRIPTION
  Runs on the laptop the board is plugged into (next to usb_flash.ps1,
  usb_diag.ps1, cdc_cmd.ps1). Needs the firmware with "diag reset", the
  "reply D2 ..." / "bus cycle_us ..." diag lines and the extended "stream"
  columns (build of 2026-10-07 or later).

  1. Finds the board COM port (USB CDC, VID_0483 PID_5740), opens it once
     (115200 8N1, DTR on) and keeps it for the whole run.
  2. "diag": both sensors (D2, D3) must be OK, the board must not be recording
     to SD (use -Force to run anyway). Remembers the current freq and gap.
  3. "set freq 50" (more than the bus can do: every cycle starts as soon as
     the bus is free, so the measured rate is the bus limit).
  4. For each gap in -Gaps: "set gap N", wait -SettleSec, "diag reset",
     "stream <StreamMs>" for -StepSec seconds (raw S lines -> *_stream.csv),
     "stream 0", "diag" -> one summary row.
  5. Extra step: "set freq 20" at gap -BaseGap (the default 15 ms).
  6. Restores the original freq and gap (also after an error), prints the
     summary table.

  Mode "watch" (-Mode watch): no setting changes; "diag reset", stream for
  -StepSec seconds, final diag. Use it while someone tilts/shakes the sensors
  to see whether the reply latency (lat/lmax columns) or the bus idle
  (idle_us: superloop busy with display redraw) depends on angle changes.

  Output (next to this script, timestamped):
    rs485_sweep_<ts>.csv          one row per step (summary)
    rs485_sweep_<ts>_stream.csv   all stream lines with step/gap/freq prefix
    rs485_sweep.log               full transcript (appended)
  ASCII only; Windows PowerShell 5.1; works as SYSTEM (MeshCentral exec or a
  scheduled task: the run takes ~5 min, longer than the exec tunnel lives):
    schtasks /create /tn BWM427_Sweep /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\Tools\fw\rs485_sweep.ps1" /sc once /st 23:59 /ru SYSTEM /f
    schtasks /run /tn BWM427_Sweep
  then read C:\Tools\fw\rs485_sweep.log and the CSV files.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File rs485_sweep.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File rs485_sweep.ps1 -Gaps 15,13,12,11,10 -StepSec 60
  powershell -NoProfile -ExecutionPolicy Bypass -File rs485_sweep.ps1 -Mode watch -StepSec 60 -StreamMs 200

  Exit codes: 0 done, 1 board not found / port error, 2 board refused a
  command (ERR), 3 sensors not both OK or board recording (see -Force),
  4 lost the board in the middle of the run
#>
param(
    [ValidateSet('sweep', 'watch')]
    [string]$Mode = 'sweep',
    [int[]]$Gaps = @(15, 10, 8, 6, 5, 4, 3),
    [int]$Freq = 50,
    [int]$BaseGap = 15,
    [int]$ExtraFreq = 20,
    [int]$StepSec = 30,
    [int]$SettleSec = 3,
    [int]$StreamMs = 1000,
    [string]$Port = '',
    [switch]$Force,
    [switch]$NoRestore,
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Continue'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutDir) { $OutDir = $ScriptDir }
$Stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$LogFile = Join-Path $OutDir 'rs485_sweep.log'
$SumFile = Join-Path $OutDir ("rs485_sweep_{0}.csv" -f $Stamp)
$StreamFile = Join-Path $OutDir ("rs485_sweep_{0}_stream.csv" -f $Stamp)

$CDC_ID = 'VID_0483&PID_5740'
$Sensors = @(2, 3)

function Log([string]$msg) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $msg
    [Console]::WriteLine($msg)
    try { Add-Content -Path $LogFile -Value $line -Encoding ASCII } catch { }
}

function Get-UsbDevices([string]$id) {
    $all = @(Get-CimInstance -ClassName Win32_PnPEntity -Filter "PNPDeviceID LIKE '%VID_0483%'" -ErrorAction SilentlyContinue)
    return @($all | Where-Object { $_.PNPDeviceID -like "USB\$id*" })
}

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
    foreach ($d in (Get-UsbDevices $CDC_ID)) {
        $com = Get-ComName $d
        Log ("found {0} [{1}] status={2} port={3}" -f $d.Name, $d.PNPDeviceID, $d.Status, $com)
        if ($com) { return $com }
    }
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
    $sp.ReadBufferSize = 65536
    $sp.Open()
    Start-Sleep -Milliseconds 100
    $sp.DiscardInBuffer()
    return $sp
}

function Close-Board($sp) {
    if ($null -eq $sp) { return }
    try { if ($sp.IsOpen) { $sp.Close() } } catch { }
    try { $sp.Dispose() } catch { }
}

$script:RxBuf = ''

# Read lines for up to $seconds. $untilEnd: stop after a line OK.../ERR...
# $quietS: do not log "S,..." lines (stream), they go to the stream CSV
function Read-Lines($sp, [double]$seconds, [bool]$untilEnd, [bool]$quietS) {
    $deadline = (Get-Date).AddSeconds($seconds)
    $lines = New-Object System.Collections.Generic.List[string]
    while ((Get-Date) -lt $deadline) {
        $chunk = ''
        try { $chunk = $sp.ReadExisting() } catch { throw "read: $($_.Exception.Message)" }
        if ($chunk.Length -eq 0) { Start-Sleep -Milliseconds 20; continue }
        $script:RxBuf += $chunk
        while (($i = $script:RxBuf.IndexOf("`n")) -ge 0) {
            $line = $script:RxBuf.Substring(0, $i).TrimEnd("`r")
            $script:RxBuf = $script:RxBuf.Substring($i + 1)
            if (-not ($quietS -and $line.StartsWith('S,'))) { Log "< $line" }
            $lines.Add($line)
            if ($untilEnd -and ($line -match '^(OK|ERR)')) { return ,$lines }
        }
    }
    return ,$lines
}

function Send-Cmd($sp, [string]$cmd, [double]$seconds) {
    Log "> $cmd"
    $sp.Write("$cmd`r`n")
    $reply = Read-Lines $sp $seconds $true $true
    if (@($reply | Where-Object { $_ -match '^ERR' }).Count -gt 0) {
        throw "board answered ERR to '$cmd'"
    }
    if (@($reply | Where-Object { $_ -match '^OK' }).Count -eq 0) {
        throw "no OK to '$cmd' within $seconds s"
    }
    return ,$reply
}

# key=value pairs of a line into a hashtable
function Get-Kv([string]$line) {
    $h = @{}
    foreach ($m in [regex]::Matches($line, '([A-Za-z_]+)=([-0-9.]+)')) { $h[$m.Groups[1].Value] = $m.Groups[2].Value }
    return $h
}

# Parse a "diag" reply into a flat hashtable
function Parse-Diag($lines) {
    $d = @{}
    foreach ($l in $lines) {
        if ($l -match '^sensor D(\d+) addr=\d+ (\w+) .* ok=(\d+) err=(\d+) garbled=(\d+)') {
            $a = $Matches[1]
            $d["st$a"] = $Matches[2]; $d["ok_total$a"] = [long]$Matches[3]; $d["err_total$a"] = [long]$Matches[4]
        } elseif ($l -match '^rate actual=([0-9.]+) Hz, log_freq=(\d+) Hz') {
            $d['rate'] = $Matches[1]; $d['freq'] = [int]$Matches[2]
        } elseif ($l -match '^bus gap (\d+) ms') {
            $d['gap'] = [int]$Matches[1]
        } elseif ($l -match '^loop (\d+)/s, max (\d+) ms') {
            $d['loops'] = [long]$Matches[1]; $d['loop_max_ms'] = [int]$Matches[2]
        } elseif ($l -match '^cpu load=(\d+)%') {
            $d['cpu'] = [int]$Matches[1]
        } elseif ($l -match '^sd (\w+)') {
            $d['sd'] = $Matches[1]
        } elseif ($l -match '^reply D(\d+) n_ok=') {
            $a = $Matches[1]; $kv = Get-Kv $l
            $d["n_ok$a"] = [long]$kv['n_ok']; $d["to$a"] = [long]$kv['timeout']
            $d["crc$a"] = [long]$kv['crc']; $d["bad$a"] = [long]$kv['bad_frame']; $d["tmo_us$a"] = [long]$kv['timeout_us']
        } elseif ($l -match '^reply D(\d+) first_us (.*) done_us (.*)$') {
            $a = $Matches[1]; $f = Get-Kv $Matches[2]; $e = Get-Kv $Matches[3]
            $d["lat_last$a"] = [long]$f['last']; $d["lat_avg$a"] = [long]$f['avg']
            $d["lat_min$a"] = [long]$f['min']; $d["lat_max$a"] = [long]$f['max']
            $d["done_avg$a"] = [long]$e['avg']; $d["done_max$a"] = [long]$e['max']
        } elseif ($l -match '^bus gap_us ') {
            $kv = Get-Kv $l; $d['gap_n'] = [long]$kv['n']; $d['gap_avg'] = [long]$kv['avg']; $d['gap_max'] = [long]$kv['max']
        } elseif ($l -match '^bus idle_us ') {
            $kv = Get-Kv $l; $d['idle_avg'] = [long]$kv['avg']; $d['idle_max'] = [long]$kv['max']
        } elseif ($l -match '^bus cycle_us ') {
            $kv = Get-Kv $l
            $d['cyc_n'] = [long]$kv['n']; $d['cyc_avg'] = [long]$kv['avg']; $d['cyc_min'] = [long]$kv['min']
            $d['cyc_max'] = [long]$kv['max']; $d['max_rate'] = $kv['max_rate']
        } elseif ($l -match '^bus timer_fallbacks=(\d+)') {
            $d['fallbacks'] = [long]$Matches[1]
        }
    }
    return $d
}

function V($d, [string]$k) { if ($d.ContainsKey($k)) { return $d[$k] } return '' }

$SumCols = @('step', 'freq_set', 'gap_ms', 'seconds', 'rate_hz', 'cycle_avg_us', 'cycle_min_us', 'cycle_max_us',
    'max_rate_hz', 'clean_cycles', 'idle_avg_us', 'idle_max_us', 'gap_avg_us', 'gap_max_us', 'cpu_pct',
    'loop_max_ms', 'timer_fallbacks')
foreach ($a in $Sensors) {
    $SumCols += @("st$a", "ok$a", "err$a", "timeouts$a", "crc$a", "bad$a", "lat_min_us$a", "lat_avg_us$a",
        "lat_max_us$a", "done_avg_us$a", "timeout_us$a")
}
$SumCols += 'verdict'
$Summary = New-Object System.Collections.Generic.List[object]

# One measurement step at the current settings
function Run-Step($sp, [string]$name, [int]$seconds) {
    $before = Parse-Diag (Send-Cmd $sp 'diag' 5)
    $null = Send-Cmd $sp 'diag reset' 3
    $t0 = Get-Date
    $hdr = Send-Cmd $sp "stream $StreamMs" 3
    $lines = Read-Lines $sp $seconds $false $true
    $null = Send-Cmd $sp 'stream 0' 3
    $secs = ((Get-Date) - $t0).TotalSeconds
    $after = Parse-Diag (Send-Cmd $sp 'diag' 5)
    $rows = @($lines | Where-Object { $_.StartsWith('S,') })
    if (-not (Test-Path -LiteralPath $StreamFile)) {
        $h = @($hdr | Where-Object { $_.StartsWith('# S,') })
        if ($h.Count -gt 0) {
            Add-Content -Path $StreamFile -Value ('step,freq_set,gap_set,' + $h[0].Substring(2)) -Encoding ASCII
        }
    }
    $prefix = '{0},{1},{2},' -f $name, (V $after 'freq'), (V $after 'gap')
    if ($rows.Count -gt 0) { Add-Content -Path $StreamFile -Value ($rows | ForEach-Object { $prefix + $_ }) -Encoding ASCII }

    $r = [ordered]@{}
    $r['step'] = $name; $r['freq_set'] = V $after 'freq'; $r['gap_ms'] = V $after 'gap'
    $r['seconds'] = [int][math]::Round($secs); $r['rate_hz'] = V $after 'rate'
    $r['cycle_avg_us'] = V $after 'cyc_avg'; $r['cycle_min_us'] = V $after 'cyc_min'; $r['cycle_max_us'] = V $after 'cyc_max'
    $r['max_rate_hz'] = V $after 'max_rate'; $r['clean_cycles'] = V $after 'cyc_n'
    $r['idle_avg_us'] = V $after 'idle_avg'; $r['idle_max_us'] = V $after 'idle_max'
    $r['gap_avg_us'] = V $after 'gap_avg'; $r['gap_max_us'] = V $after 'gap_max'
    $r['cpu_pct'] = V $after 'cpu'; $r['loop_max_ms'] = V $after 'loop_max_ms'; $r['timer_fallbacks'] = V $after 'fallbacks'
    $bad = 0
    foreach ($a in $Sensors) {
        $r["st$a"] = V $after "st$a"
        $r["ok$a"] = V $after "n_ok$a"
        $e = ''
        if ($before.ContainsKey("err_total$a") -and $after.ContainsKey("err_total$a")) { $e = $after["err_total$a"] - $before["err_total$a"] }
        $r["err$a"] = $e
        $r["timeouts$a"] = V $after "to$a"; $r["crc$a"] = V $after "crc$a"; $r["bad$a"] = V $after "bad$a"
        $r["lat_min_us$a"] = V $after "lat_min$a"; $r["lat_avg_us$a"] = V $after "lat_avg$a"; $r["lat_max_us$a"] = V $after "lat_max$a"
        $r["done_avg_us$a"] = V $after "done_avg$a"; $r["timeout_us$a"] = V $after "tmo_us$a"
        $errs = 0
        foreach ($k in @("to$a", "crc$a", "bad$a")) { if ($after.ContainsKey($k)) { $errs += $after[$k] } }
        if ($errs -gt 0 -or (V $after "st$a") -ne 'OK') { $bad++ }
    }
    if ($bad -eq 0) { $r['verdict'] = 'CLEAN' } else { $r['verdict'] = 'ERRORS' }
    $Summary.Add([pscustomobject]$r)
    $line = ($SumCols | ForEach-Object { $r[$_] }) -join ','
    if (-not (Test-Path -LiteralPath $SumFile)) { Add-Content -Path $SumFile -Value ($SumCols -join ',') -Encoding ASCII }
    Add-Content -Path $SumFile -Value $line -Encoding ASCII
    Log ("STEP {0}: freq={1} gap={2} rate={3} Hz cycle={4} us (max {5} Hz) idle_avg={6} us | D2 ok={7} to={8} crc={9} lat {10}/{11}/{12} us | D3 ok={13} to={14} crc={15} lat {16}/{17}/{18} us | cpu {19}% -> {20}" -f `
        $name, $r['freq_set'], $r['gap_ms'], $r['rate_hz'], $r['cycle_avg_us'], $r['max_rate_hz'], $r['idle_avg_us'],
        $r['ok2'], $r['timeouts2'], $r['crc2'], $r['lat_min_us2'], $r['lat_avg_us2'], $r['lat_max_us2'],
        $r['ok3'], $r['timeouts3'], $r['crc3'], $r['lat_min_us3'], $r['lat_avg_us3'], $r['lat_max_us3'],
        $r['cpu_pct'], $r['verdict'])
}

# ---------------------------------------------------------------------------
Log "=== rs485_sweep $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') mode=$Mode gaps=$($Gaps -join ',') freq=$Freq step=${StepSec}s stream=${StreamMs}ms (user $env:USERNAME)"
if (-not $Port) { $Port = Find-BoardPort }
if (-not $Port) { Log "ERROR: no USB device $CDC_ID (board COM port) found"; exit 1 }

$sp = $null
$rc = 0
$origFreq = $null
$origGap = $null
try {
    $sp = Open-Board $Port
    Log "opened $Port"
    $null = Send-Cmd $sp 'stream 0' 3
    $null = Send-Cmd $sp 'ver' 3
    $d0 = Parse-Diag (Send-Cmd $sp 'diag' 5)
    $origFreq = V $d0 'freq'
    $origGap = V $d0 'gap'
    Log "start: freq=$origFreq Hz gap=$origGap ms D2=$(V $d0 'st2') D3=$(V $d0 'st3') sd=$(V $d0 'sd')"
    if (-not $d0.ContainsKey('cyc_n')) {
        Log "ERROR: firmware without the bus timing diag (reply/bus cycle_us lines): flash the 2026-10-07 build first"
        $rc = 2
        throw 'old firmware'
    }
    $notOk = @($Sensors | Where-Object { (V $d0 "st$_") -ne 'OK' })
    if ($notOk.Count -gt 0 -and -not $Force) {
        Log "ERROR: sensor(s) $($notOk -join ',') not OK - connect both sensors (or -Force)"
        $rc = 3
        throw 'sensors'
    }
    if ((V $d0 'sd') -eq 'RECORDING' -and -not $Force) {
        Log "ERROR: the board is recording to SD - not changing its settings (stop recording or -Force)"
        $rc = 3
        throw 'recording'
    }

    if ($Mode -eq 'watch') {
        Run-Step $sp 'watch' $StepSec
    } else {
        $null = Send-Cmd $sp "set freq $Freq" 3
        foreach ($g in $Gaps) {
            $null = Send-Cmd $sp "set gap $g" 3
            Start-Sleep -Seconds $SettleSec
            $null = Read-Lines $sp 0.2 $false $true
            Run-Step $sp ("gap{0}" -f $g) $StepSec
        }
        if ($ExtraFreq -gt 0) {
            $null = Send-Cmd $sp "set gap $BaseGap" 3
            $null = Send-Cmd $sp "set freq $ExtraFreq" 3
            Start-Sleep -Seconds $SettleSec
            Run-Step $sp ("freq{0}_gap{1}" -f $ExtraFreq, $BaseGap) $StepSec
        }
    }
} catch {
    if ($rc -eq 0) {
        Log "ERROR: $($_.Exception.Message)"
        if ($_.Exception.Message -match 'ERR') { $rc = 2 } else { $rc = 4 }
    }
} finally {
    if ($null -ne $sp -and $Mode -eq 'sweep' -and -not $NoRestore -and $origFreq -and $origGap) {
        try {
            if (-not $sp.IsOpen) { $sp.Open() }
            $null = Send-Cmd $sp 'stream 0' 3
            $null = Send-Cmd $sp "set gap $origGap" 3
            $null = Send-Cmd $sp "set freq $origFreq" 3
            Log "restored freq=$origFreq Hz gap=$origGap ms (saved to flash 3 s later, not while recording)"
        } catch {
            Log "WARNING: could not restore freq=$origFreq gap=${origGap}: $($_.Exception.Message) - do it by hand: set gap $origGap / set freq $origFreq"
        }
    }
    Close-Board $sp
}

if ($Summary.Count -gt 0) {
    Log "--- summary ($SumFile) ---"
    Log "step              rate   cycle_us  max_rate  idle_avg | D2 ok/to/crc  lat min/avg/max us   | D3 ok/to/crc  lat min/avg/max us   | cpu verdict"
    foreach ($r in $Summary) {
        Log ("{0,-16} {1,6} {2,9} {3,8} {4,9} | {5,5}/{6}/{7}  {8,6}/{9,6}/{10,6} | {11,5}/{12}/{13}  {14,6}/{15,6}/{16,6} | {17,3} {18}" -f `
            $r.step, $r.rate_hz, $r.cycle_avg_us, $r.max_rate_hz, $r.idle_avg_us,
            $r.ok2, $r.timeouts2, $r.crc2, $r.lat_min_us2, $r.lat_avg_us2, $r.lat_max_us2,
            $r.ok3, $r.timeouts3, $r.crc3, $r.lat_min_us3, $r.lat_avg_us3, $r.lat_max_us3,
            $r.cpu_pct, $r.verdict)
    }
    $clean = @($Summary | Where-Object { $_.verdict -eq 'CLEAN' -and $_.step -like 'gap*' } | ForEach-Object { [int]$_.gap_ms })
    $dirty = @($Summary | Where-Object { $_.verdict -ne 'CLEAN' -and $_.step -like 'gap*' } | ForEach-Object { [int]$_.gap_ms })
    if ($dirty.Count -gt 0) {
        $worst = ($dirty | Measure-Object -Maximum).Maximum
        $cand = @($clean | Where-Object { $_ -ge 2 * $worst -and $_ -ge 5 })
        if ($cand.Count -gt 0) { $rec = ($cand | Measure-Object -Minimum).Minimum } else { $rec = $BaseGap }
        Log "largest failing gap $worst ms -> suggested gap (clean, >= 2x failing, >= 5 ms): $rec ms"
    } elseif ($clean.Count -gt 0) {
        Log "no failing gap in this run (smallest clean $(($clean | Measure-Object -Minimum).Minimum) ms); confirm the chosen gap with a 60+ s run"
    }
}
Log "=== done, exit $rc"
exit $rc
