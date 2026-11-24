param([string]$Exe = 'build\rg808.exe')

$ErrorActionPreference = 'Stop'

# Core check: the UI playhead follows the swung hit, not the straight grid.
& $Exe --test-swing
if ($LASTEXITCODE -ne 0) { throw 'swing playhead self-test failed' }
'PASS: swing playhead'
