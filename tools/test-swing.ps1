$ErrorActionPreference = 'Stop'

# Core check: the UI playhead follows the swung hit, not the straight grid.
& build\rg808.exe --test-swing
if ($LASTEXITCODE -ne 0) { throw 'swing playhead self-test failed' }
'PASS: swing playhead'
