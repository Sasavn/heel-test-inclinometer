# Wait until the BWM427 board shows up on USB (CDC 0483:5740 or DFU 0483:DF11)
# and flash bwm427_new.bin (next to this script) with usb_flash.ps1.
# Retries while the board is absent (exit 1) or busy recording (exit 2);
# stops on success or on any other error. Runs non-interactively (SYSTEM task).
param(
	[int]$MaxHours = 6,
	[int]$PollSec = 15
)
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$log = Join-Path $here 'wait_and_flash.log'
function Log($m) {
	$line = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' ' + $m
	Add-Content -Path $log -Value $line
}

$deadline = (Get-Date).AddHours($MaxHours)
Log "=== waiting for board (max $MaxHours h), image $(Join-Path $here 'bwm427_new.bin')"
while ((Get-Date) -lt $deadline) {
	$dev = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
		Where-Object { $_.InstanceId -match 'VID_0483&PID_(5740|DF11)' })
	if ($dev.Count -gt 0) {
		Log "board present: $($dev[0].FriendlyName); running usb_flash.ps1"
		& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'usb_flash.ps1')
		$code = $LASTEXITCODE
		Log "usb_flash.ps1 exit $code"
		if ($code -eq 0) {
			Log '=== done: flashed'
			exit 0
		}
		if ($code -ne 1 -and $code -ne 2) {
			Log '=== stopped: flash error, see usb_flash.log'
			exit $code
		}
		Start-Sleep -Seconds 30
		continue
	}
	Start-Sleep -Seconds $PollSec
}
Log '=== timeout, board never appeared'
exit 1
