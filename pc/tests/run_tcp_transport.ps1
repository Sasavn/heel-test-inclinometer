$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$paths = @("$root/src/comm/TcpConnection.hpp", "$PSScriptRoot/tcp_transport_test.cpp")
$meta = @($paths | ForEach-Object { $f=Get-Item -LiteralPath $_; [pscustomobject]@{Path=$f.FullName;Creation=$f.CreationTimeUtc;Write=$f.LastWriteTimeUtc;Access=$f.LastAccessTimeUtc} })
$log = "$PSScriptRoot/tcp_transport_run.log"
try {
 "UTC=$([DateTime]::UtcNow.ToString('o'))`nHEAD=$(git -C $root rev-parse HEAD)`nCompiler=$(g++ -dumpfullversion)`nCommand=g++ -std=c++17 -D_WIN32_WINNT=0x0601 -Isrc tests/tcp_transport_test.cpp src/comm/DeviceLink.cpp -lws2_32" | Set-Content $log
 & g++ -std=c++17 -D_WIN32_WINNT=0x0601 "-I$root/src" "$PSScriptRoot/tcp_transport_test.cpp" "$root/src/comm/DeviceLink.cpp" -lws2_32 -o "$env:TEMP/tab5_tcp_transport_test.exe"
 if($LASTEXITCODE -ne 0){throw 'compile failed'}
 & "$env:TEMP/tab5_tcp_transport_test.exe" | Tee-Object -FilePath $log -Append
 if($LASTEXITCODE -ne 0){throw 'test failed'}
 foreach($p in $paths){Get-FileHash -LiteralPath $p -Algorithm SHA256 | Format-List | Out-String | Add-Content $log}
 Get-FileHash "$env:TEMP/tab5_tcp_transport_test.exe" | Format-List | Out-String | Add-Content $log
} finally {
 foreach($m in $meta){$f=Get-Item -LiteralPath $m.Path;$f.CreationTimeUtc=$m.Creation;$f.LastWriteTimeUtc=$m.Write;$f.LastAccessTimeUtc=$m.Access}
 $meta | ConvertTo-Json | Add-Content $log
}
