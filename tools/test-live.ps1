$p = Start-Process build\rg808.exe -PassThru -RedirectStandardError build\live-err.txt
Start-Sleep -Seconds 4
if (-not $p.HasExited) { Stop-Process -Id $p.Id; 'alive after 4s -> killed (OK)' }
else { 'exited early, code ' + $p.ExitCode }
if (Test-Path build\live-err.txt) { 'stderr:'; Get-Content build\live-err.txt }
